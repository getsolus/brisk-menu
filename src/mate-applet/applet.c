/*
 * This file is part of brisk-menu.
 *
 * Copyright © 2016-2020 Brisk Menu Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define _GNU_SOURCE

#include "config.h"
#include "util.h"

BRISK_BEGIN_PEDANTIC
#include "applet.h"
#include "frontend/classic/classic-window.h"
#include "frontend/dash/dash-window.h"
#include "lib/authors.h"
#include "lib/styles.h"
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>
BRISK_END_PEDANTIC

G_DEFINE_TYPE(BriskMenuApplet, brisk_menu_applet, PANEL_TYPE_APPLET)

static gint icon_sizes[] = { 16, 24, 32, 48, 64, 96, 128, 256 };

DEF_AUTOFREE(NotifyNotification, g_object_unref)

/**
 * Handle showing of the menu
 */
static gboolean button_press_cb(BriskMenuApplet *self, GdkEvent *event, gpointer v);
static void brisk_menu_applet_change_orient(MatePanelApplet *applet, MatePanelAppletOrient orient);
static void brisk_menu_applet_change_size(MatePanelApplet *applet, guint size);
static void brisk_menu_applet_change_menu_orient(BriskMenuApplet *self);

static gboolean brisk_menu_applet_startup(BriskMenuApplet *self);
static void brisk_menu_applet_create_window(BriskMenuApplet *self);

/* Handle applet settings */
void brisk_menu_applet_init_settings(BriskMenuApplet *self);
static void brisk_menu_applet_update_icon(BriskMenuApplet *self);
static void brisk_menu_applet_settings_changed(GSettings *settings, const gchar *key, gpointer v);
static void brisk_menu_applet_notify_fail(const gchar *title, const gchar *body);

/* Helpers */
static GtkPositionType convert_mate_position(MatePanelAppletOrient orient);
static void brisk_menu_applet_adapt_layout(BriskMenuApplet *self);
static GtkWidget *brisk_menu_applet_automatic_window_type(BriskMenuApplet *self);

/**
 * brisk_menu_applet_dispose:
 *
 * Clean up a BriskMenuApplet instance
 */
static void brisk_menu_applet_dispose(GObject *obj)
{
        BriskMenuApplet *self = NULL;

        self = BRISK_MENU_APPLET(obj);

        /* Tear down the menu */
        if (self->menu) {
                gtk_widget_hide(self->menu);
                g_clear_pointer(&self->menu, gtk_widget_destroy);
        }

        g_clear_object(&self->settings);

        G_OBJECT_CLASS(brisk_menu_applet_parent_class)->dispose(obj);
}

/**
 * brisk_menu_applet_class_init:
 *
 * Handle class initialisation
 */
static void brisk_menu_applet_class_init(BriskMenuAppletClass *klazz)
{
        GObjectClass *obj_class = G_OBJECT_CLASS(klazz);
        MatePanelAppletClass *mate_class = MATE_PANEL_APPLET_CLASS(klazz);

        /* gobject vtable hookup */
        obj_class->dispose = brisk_menu_applet_dispose;

        /* mate vtable hookup */
        mate_class->change_orient = brisk_menu_applet_change_orient;
        mate_class->change_size = brisk_menu_applet_change_size;
}

void brisk_menu_applet_init_settings(BriskMenuApplet *self)
{
        self->settings = g_settings_new("com.solus-project.brisk-menu");

        /* capture changes in settings that affect the menu applet */
        g_signal_connect(self->settings,
                         "changed::label-text",
                         G_CALLBACK(brisk_menu_applet_settings_changed),
                         self);

        g_signal_connect(self->settings,
                         "changed::window-type",
                         G_CALLBACK(brisk_menu_applet_settings_changed),
                         self);

        g_signal_connect(self->settings,
                         "changed::icon-name",
                         G_CALLBACK(brisk_menu_applet_settings_changed),
                         self);

        g_signal_connect(self->settings,
                         "changed::icon-symbolic",
                         G_CALLBACK(brisk_menu_applet_settings_changed),
                         self);
}

/**
 * brisk_menu_applet_init:
 *
 * Handle construction of the BriskMenuApplet
 */
static void brisk_menu_applet_init(BriskMenuApplet *self)
{
        GtkWidget *toggle, *layout, *image, *label = NULL;
        GtkStyleContext *style = NULL;

        brisk_menu_applet_init_settings(self);

        /* Create the toggle button */
        toggle = gtk_toggle_button_new();
        self->toggle = toggle;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), FALSE);
        gtk_container_add(GTK_CONTAINER(self), toggle);
        g_signal_connect_swapped(toggle, "button-press-event", G_CALLBACK(button_press_cb), self);
        gtk_button_set_relief(GTK_BUTTON(toggle), GTK_RELIEF_NONE);
        style = gtk_widget_get_style_context(toggle);
        gtk_style_context_add_class(style, BRISK_STYLE_BUTTON);

        /* Layout will contain icon + label */
        layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_halign(layout, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(toggle), layout);

        /* Image appears first always */
        image = gtk_image_new_from_icon_name("start-here-symbolic", GTK_ICON_SIZE_MENU);
        self->image = image;
        gtk_box_pack_start(GTK_BOX(layout), image, FALSE, FALSE, 0);
        gtk_widget_set_margin_end(image, 4);
        gtk_widget_set_halign(image, GTK_ALIGN_START);

        /* Now add the label */
        label = gtk_label_new(NULL);
        self->label = label;
        gtk_box_pack_start(GTK_BOX(layout), label, TRUE, TRUE, 0);
        gtk_widget_set_margin_end(label, 4);
        /* Set it up for visibility toggling */
        gtk_widget_show_all(label);
        gtk_widget_set_no_show_all(label, TRUE);
        gtk_widget_hide(label);

        /* Update label visibility dependent on config */
        g_settings_bind(self->settings, "label-visible", label, "visible", G_SETTINGS_BIND_GET);

        /* Pump the label setting */
        brisk_menu_applet_settings_changed(self->settings, "label-text", self);

        /* Update the icon with the requested value. */
        brisk_menu_applet_settings_changed(self->settings, "icon-name", self);

        /* Fix label alignment */
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
        G_GNUC_END_IGNORE_DEPRECATIONS

        /* Applet hookup */
        mate_panel_applet_set_flags(MATE_PANEL_APPLET(self), MATE_PANEL_APPLET_EXPAND_MINOR);
        mate_panel_applet_set_background_widget(MATE_PANEL_APPLET(self), GTK_WIDGET(self));

        /* Wait for mate-panel to do its thing and tell us the orientation */
        g_idle_add((GSourceFunc)brisk_menu_applet_startup, self);
}

static gboolean brisk_menu_applet_startup(BriskMenuApplet *self)
{
        /* Ensure we fire off the initial layout adaptation code */
        brisk_menu_applet_change_orient(MATE_PANEL_APPLET(self),
                                        mate_panel_applet_get_orient(MATE_PANEL_APPLET(self)));

        return G_SOURCE_REMOVE;
}

static void brisk_menu_applet_create_window(BriskMenuApplet *self)
{
        GtkWidget *menu = NULL;

        /* Now show all content */
        gtk_widget_show_all(self->toggle);

        /* Construct our menu */
        WindowType window_type = g_settings_get_enum(self->settings, "window-type");
        switch (window_type) {
        case WINDOW_TYPE_DASH:
                menu = GTK_WIDGET(brisk_dash_window_new(GTK_WIDGET(self)));
                break;
        case WINDOW_TYPE_AUTOMATIC:
                menu = brisk_menu_applet_automatic_window_type(self);
                break;
        case WINDOW_TYPE_CLASSIC:
        default:
                menu = GTK_WIDGET(brisk_classic_window_new(GTK_WIDGET(self)));
                break;
        }

        self->menu = menu;

        /* Render "active" toggle only when the window is open, automatically. */
        g_object_bind_property(menu, "visible", self->toggle, "active", G_BINDING_DEFAULT);

        /* Load our menus */
        brisk_menu_window_load_menus(BRISK_MENU_WINDOW(self->menu));

        /* Pump the settings */
        brisk_menu_window_pump_settings(BRISK_MENU_WINDOW(self->menu));

        /* Now that the menu is initialised, we can tell it to update to our current
         * orientation, so that automatic position is correct on first start */
        brisk_menu_applet_change_menu_orient(self);
}

/**
 * Toggle the menu visibility on a button press
 */
static gboolean button_press_cb(BriskMenuApplet *self, GdkEvent *event, __brisk_unused__ gpointer v)
{
        if (event->button.button != 1) {
                return GDK_EVENT_PROPAGATE;
        }

        gboolean vis = !gtk_widget_get_visible(self->menu);
        if (vis) {
                brisk_menu_window_update_screen_position(BRISK_MENU_WINDOW(self->menu));
        }

        gtk_widget_set_visible(self->menu, vis);

        return GDK_EVENT_STOP;
}

static void brisk_menu_applet_update_icon(BriskMenuApplet *self) {
        autofree(gchar) *icon_name = NULL,
                        *icon_name_tmp = NULL;

        icon_name = g_settings_get_string(self->settings, "icon-name");
        if (g_str_equal(icon_name, "")) {
                g_free(icon_name);
                icon_name = g_strdup("start-here");
        }
        if (g_settings_get_boolean(self->settings, "icon-symbolic")) {
                icon_name_tmp = g_strdup(icon_name);
                g_free(icon_name);
                icon_name = g_strconcat(icon_name_tmp, "-symbolic", NULL);
        }
        gtk_image_set_from_icon_name(GTK_IMAGE(self->image), icon_name, GTK_ICON_SIZE_MENU);
}

/**
 * Callback for changing applet settings
 */
static void brisk_menu_applet_settings_changed(GSettings *settings, const gchar *key, gpointer v)
{
        BriskMenuApplet *self = v;

        if (g_str_equal(key, "label-text")) {
                autofree(gchar) *value = NULL;
                value = g_settings_get_string(settings, key);

                if (g_str_equal(value, "")) {
                        gtk_label_set_text(GTK_LABEL(self->label), _("Menu"));
                } else {
                        gtk_label_set_text(GTK_LABEL(self->label), value);
                }
        } else if (g_str_equal(key, "window-type")) {
                gtk_widget_hide(self->menu);
                g_clear_pointer(&self->menu, gtk_widget_destroy);
                brisk_menu_applet_create_window(self);
        } else if (g_str_equal(key, "icon-name")) {
                brisk_menu_applet_update_icon(self);
        } else if (g_str_equal(key, "icon-symbolic")) {
                brisk_menu_applet_update_icon(self);
        }
}

/**
 * Internal helper to ensure the orient is correct for the menu
 */
static void brisk_menu_applet_change_menu_orient(BriskMenuApplet *self)
{
        GtkPositionType position;
        position = convert_mate_position(self->orient);

        /* Let the main menu window know about our orientation */
        brisk_menu_window_set_parent_position(BRISK_MENU_WINDOW(self->menu), position);
}

/**
 * Panel orientation changed, tell the menu
 */
static void brisk_menu_applet_change_orient(MatePanelApplet *applet, MatePanelAppletOrient orient)
{
        BriskMenuApplet *self = BRISK_MENU_APPLET(applet);
        self->orient = orient;
        /* Now adjust our own display to deal with the orientation */
        brisk_menu_applet_adapt_layout(BRISK_MENU_APPLET(applet));

        if (!self->menu) {
                brisk_menu_applet_create_window(self);
                return;
        }

        brisk_menu_applet_change_menu_orient(self);
}

static void brisk_menu_applet_change_size(MatePanelApplet *applet, guint size)
{
        BriskMenuApplet *self = BRISK_MENU_APPLET(applet);

        gint final_size = icon_sizes[0];

        for (guint i = 0; i < G_N_ELEMENTS(icon_sizes); i++) {
                if (icon_sizes[i] > (gint)size - 2) {
                        break;
                }
                final_size = icon_sizes[i];
        }

        gtk_image_set_pixel_size(GTK_IMAGE(self->image), final_size);
}

void brisk_menu_applet_edit_menus(__brisk_unused__ GtkAction *action, BriskMenuApplet *self)
{
        static const char *editors[] = {
                "menulibre.desktop",
                "mozo.desktop",
        };
        static const char *binaries[] = {
                "menulibre",
                "mozo",
        };
        for (size_t i = 0; i < G_N_ELEMENTS(editors); i++) {
                autofree(gchar) *p = NULL;
                autofree(GAppInfo) *app = NULL;
                BriskMenuLauncher *launcher = ((BRISK_MENU_WINDOW(self->menu))->launcher);
                GDesktopAppInfo *info = NULL;

                p = g_find_program_in_path(binaries[i]);
                if (!p) {
                        continue;
                }

                info = g_desktop_app_info_new(editors[i]);
                if (!info) {
                        app = g_app_info_create_from_commandline(p,
                                                                 NULL,
                                                                 G_APP_INFO_CREATE_NONE,
                                                                 NULL);
                } else {
                        app = G_APP_INFO(info);
                }
                if (!app) {
                        continue;
                }
                brisk_menu_launcher_start(launcher, GTK_WIDGET(self), app);
                return;
        }

        brisk_menu_applet_notify_fail(_("Failed to launch menu editor"),
                                      _("Please install 'menulibre' or 'mozo' to edit menus"));
}

/**
 * brisk_menu_applet_notify_fail:
 *
 * Notify the user that an action has failed via a passive notification
 */
static void brisk_menu_applet_notify_fail(const gchar *title, const gchar *body)
{
        autofree(NotifyNotification) *notif = NULL;
        autofree(GError) *error = NULL;

        notif = notify_notification_new(title, body, "dialog-error-symbolic");
        notify_notification_set_timeout(notif, 4000);
        if (!notify_notification_show(notif, &error)) {
                g_message("Failed to send notification: %s", error->message);
                fprintf(stderr, "\tTitle: %s\n\tBody: %s\n", title, body);
        }
}

void brisk_menu_applet_show_about(__brisk_unused__ GtkAction *action,
                                  BriskMenuApplet *applet)
{
        static const gchar *copyright_string = "Copyright © 2016-2020 Brisk Menu Developers";
        autofree(gchar) *icon_name = NULL;

        icon_name = g_settings_get_string(applet->settings, "icon-name");
        if (g_str_equal(icon_name, "")) {
                g_free(icon_name);
                icon_name = g_strdup("start-here");
        }

        gtk_show_about_dialog(NULL,
                              "authors",
                              brisk_developers,
                              "copyright",
                              copyright_string,
                              "license-type",
                              GTK_LICENSE_GPL_2_0,
                              "logo-icon-name",
                              icon_name,
                              "version",
                              PACKAGE_VERSION,
                              "website",
                              PACKAGE_URL,
                              "website-label",
                              "Solus Project",
                              NULL);
}

/**
 * Convert the MatePanelAppletOrient into a more logical GtkPositionType.
 *
 * This converts the "orient", i.e. "where is my panel looking", to an actual
 * position that Brisk can use. Additionally it removes the need to have the
 * frontend library depend on mate-panel-applet.
 */
static GtkPositionType convert_mate_position(MatePanelAppletOrient orient)
{
        switch (orient) {
        case MATE_PANEL_APPLET_ORIENT_LEFT:
                return GTK_POS_RIGHT;
        case MATE_PANEL_APPLET_ORIENT_RIGHT:
                return GTK_POS_LEFT;
        case MATE_PANEL_APPLET_ORIENT_DOWN:
                return GTK_POS_TOP;
        case MATE_PANEL_APPLET_ORIENT_UP:
        default:
                return GTK_POS_BOTTOM;
        }
}

/**
 * brisk_menu_applet_adapt_layout:
 *
 * Update our layout in response to an orientation change.
 * Primarily we're hiding our label automatically here and maximizing the space
 * available to the icon.
 */
static void brisk_menu_applet_adapt_layout(BriskMenuApplet *self)
{
        GtkStyleContext *style = NULL;

        style = gtk_widget_get_style_context(self->toggle);

        switch (self->orient) {
        case MATE_PANEL_APPLET_ORIENT_LEFT:
        case MATE_PANEL_APPLET_ORIENT_RIGHT:
                /* Handle vertical panel layout */
                gtk_widget_hide(self->label);
                gtk_widget_set_halign(self->image, GTK_ALIGN_CENTER);
                gtk_style_context_add_class(style, BRISK_STYLE_BUTTON_VERTICAL);
                gtk_widget_set_margin_end(self->image, 0);
                break;
        default:
                /* We're a horizontal panel */
                gtk_widget_set_visible(self->label,
                                       g_settings_get_boolean(self->settings, "label-visible"));
                gtk_widget_set_halign(self->image, GTK_ALIGN_START);
                gtk_style_context_remove_class(style, BRISK_STYLE_BUTTON_VERTICAL);
                gtk_widget_set_margin_end(self->image, 4);
                break;
        }
}

static GtkWidget *brisk_menu_applet_automatic_window_type(BriskMenuApplet *self)
{
        switch (self->orient) {
        case MATE_PANEL_APPLET_ORIENT_LEFT:
        case MATE_PANEL_APPLET_ORIENT_RIGHT:
                return GTK_WIDGET(brisk_dash_window_new(GTK_WIDGET(self)));
        default:
                return GTK_WIDGET(brisk_classic_window_new(GTK_WIDGET(self)));
        }
}
/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
