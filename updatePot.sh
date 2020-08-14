#!/bin/bash

function do_gettext()
{
    xgettext --package-name=brisk-menu --package-version=0.6.2 $* --default-domain=brisk-menu --join-existing --from-code=UTF-8 --no-wrap --keyword=_
}

function do_intltool()
{
    intltool-extract --type=$1 $2
}

rm brisk-menu.po -f
touch brisk-menu.po

for file in `find src -name "*.c"`; do
    do_gettext $file --add-comments --language=C
done

for file in `find src -name "*.ui"`; do
    if [[ `grep -F "translatable=\"yes\"" $file` ]]; then
        do_intltool gettext/glade $file
        do_gettext ${file}.h --add-comments --keyword=N_:1
        rm $file.h
    fi
done

for file in `find src -name "*.in"`; do
    if [[ `grep -E "^_*" $file` ]]; then
        do_intltool gettext/keys $file
        do_gettext ${file}.h --add-comments --keyword=N_:1
        rm $file.h
    fi
done

mv brisk-menu.po ../brisk-menu-translations/brisk-menu.pot