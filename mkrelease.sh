#!/usr/bin/env bash
set -e

# Script for ikey because he went with meson. *shrug*
VERSION=v$(grep "version:" meson.build | head -n1 | cut -d"'" -f2)
NAME="brisk-menu"
git archive --format tar --prefix ${NAME}-${VERSION}/ --verbose HEAD --output ${NAME}-${VERSION}.tar
xz -9 -f "${NAME}-${VERSION}.tar"

gpg --armor --detach-sign "${NAME}-${VERSION}.tar.xz"
gpg --verify "${NAME}-${VERSION}.tar.xz.asc"