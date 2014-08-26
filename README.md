Source for making / installing the dkms version of the driver where the usbnet.c is already fixed.

Basic install requires that you copy asic0x.c, dkms.conf, Makefile into /usr/src/asic0x-1.0

dkms add -m asic0x -v 1.0
dkms build -m asic0x -v 1.0
dkms install -m asic0x -v 1.0
