#!/usr/bin/bash

DRIVER_VERSION=0.0.1

DRIVER_NAME=ar0234

echo "Uninstalling any previous ${DRIVER_NAME} module"

sudo dkms remove -m ${DRIVER_NAME} -v ${DRIVER_VERSION} --all

sudo mkdir -p /usr/src/${DRIVER_NAME}-${DRIVER_VERSION}

sudo cp -r $(pwd)/* /usr/src/${DRIVER_NAME}-${DRIVER_VERSION}

# Enable post install script execute permissions
chmod +x /usr/src/${DRIVER_NAME}-${DRIVER_VERSION}/dkms.postinst

sudo dkms add -m ${DRIVER_NAME} -v ${DRIVER_VERSION}
sudo dkms build -m ${DRIVER_NAME} -v ${DRIVER_VERSION}
sudo dkms install -m ${DRIVER_NAME} -v ${DRIVER_VERSION}