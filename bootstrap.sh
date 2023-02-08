#!/bin/sh
 
sudo apt install -y intltool pkg-config libjson-c-dev libv4l-dev libudev-dev libusb-1.0-0 libusb-1.0-0-dev libdrm-dev libtool libgettextpo0 libgettextpo-dev libglib2.0-dev

# To be safe include -I flag
autoreconf --force --verbose --install
./configure --config-cache $*
