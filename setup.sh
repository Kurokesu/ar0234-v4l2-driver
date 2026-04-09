#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026, UAB Kurokesu. All rights reserved.
#
# Install AR0234 camera driver (device tree overlay + kernel module via DKMS)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

PACKAGE_NAME=$(grep '^PACKAGE_NAME=' "$SCRIPT_DIR/dkms.conf" | cut -d'=' -f2)
VERSION=$(grep '^PACKAGE_VERSION=' "$SCRIPT_DIR/dkms.conf" | cut -d'=' -f2)
DKMS_SRC="/usr/src/${PACKAGE_NAME}-${VERSION}"

if ! command -v dkms &>/dev/null; then
    echo "Error: dkms is not installed. Install it with: sudo apt install -y --no-install-recommends dkms"
    exit 1
fi

OLD_VER=$(dkms status -m "$PACKAGE_NAME" 2>/dev/null | cut -d'/' -f2 | cut -d',' -f1)
if [ -n "$OLD_VER" ]; then
    echo "Removing previous DKMS registration: ${PACKAGE_NAME}/${OLD_VER}"
    sudo dkms remove "${PACKAGE_NAME}/${OLD_VER}" --all || true
fi

echo "Copying driver source to ${DKMS_SRC}"
sudo rm -rf "$DKMS_SRC"
sudo mkdir -p "$DKMS_SRC"
sudo cp "$SCRIPT_DIR/dkms.conf" "$DKMS_SRC/"
sudo cp "$SCRIPT_DIR/dkms.postinst" "$DKMS_SRC/"
sudo cp "$SCRIPT_DIR/Makefile" "$DKMS_SRC/"
sudo cp "$SCRIPT_DIR/ar0234.c" "$DKMS_SRC/"
sudo cp "$SCRIPT_DIR/ar0234-overlay.dts" "$DKMS_SRC/"
sudo chmod +x "$DKMS_SRC/dkms.postinst"

echo "DKMS: adding ${PACKAGE_NAME}/${VERSION}"
sudo dkms add -m "$PACKAGE_NAME" -v "$VERSION"

echo "DKMS: building ${PACKAGE_NAME}/${VERSION}"
sudo dkms build -m "$PACKAGE_NAME" -v "$VERSION"

echo "DKMS: installing ${PACKAGE_NAME}/${VERSION}"
sudo dkms install -m "$PACKAGE_NAME" -v "$VERSION"

echo ""
echo "Done."
