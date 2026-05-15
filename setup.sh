#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026, UAB Kurokesu. All rights reserved.
#
# Install camera driver (device tree overlay + kernel module via DKMS)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Status line formatter (matches Makefile's PRINT)
print() { printf '  %-7s %s\n' "$1" "$2"; }

PACKAGE_NAME=$(grep '^PACKAGE_NAME=' "$SCRIPT_DIR/dkms.conf" | cut -d'"' -f2)
VERSION=$(grep '^PACKAGE_VERSION=' "$SCRIPT_DIR/dkms.conf" | cut -d'"' -f2)

if [ -z "$PACKAGE_NAME" ] || [ -z "$VERSION" ]; then
	echo "Error: Failed to read PACKAGE_NAME or PACKAGE_VERSION from dkms.conf"
	exit 1
fi

DKMS_SRC="/usr/src/${PACKAGE_NAME}-${VERSION}"

if ! command -v dkms >/dev/null 2>&1; then
	echo "Error: dkms is not installed. Install it with: sudo apt install -y --no-install-recommends dkms"
	exit 1
fi

OLD_VER=$(dkms status -m "$PACKAGE_NAME" 2>/dev/null | cut -d'/' -f2 | cut -d',' -f1)
if [ -n "$OLD_VER" ]; then
	print DKMS "remove ${PACKAGE_NAME}/${OLD_VER} (previous)"
	dkms remove "${PACKAGE_NAME}/${OLD_VER}" --all || true
fi

print COPY "driver source -> $DKMS_SRC"
rm -rf "$DKMS_SRC"
mkdir -p "$DKMS_SRC"
cp "$SCRIPT_DIR/dkms.conf" "$DKMS_SRC/"
cp "$SCRIPT_DIR/dkms.postinst" "$DKMS_SRC/"
cp "$SCRIPT_DIR/Makefile" "$DKMS_SRC/"
cp "$SCRIPT_DIR"/*.c "$DKMS_SRC/"
cp "$SCRIPT_DIR"/*.dts "$DKMS_SRC/"

print DKMS "add ${PACKAGE_NAME}/${VERSION}"
dkms add -m "$PACKAGE_NAME" -v "$VERSION"

print DKMS "build ${PACKAGE_NAME}/${VERSION}"
dkms build -m "$PACKAGE_NAME" -v "$VERSION"

print DKMS "install ${PACKAGE_NAME}/${VERSION}"
dkms install -m "$PACKAGE_NAME" -v "$VERSION"

echo ""
echo "Done."
