This is the vanilla version of libusb for brickd build with Android NDK r17.

Based on libusb's github.com commit 0034b2afdcdb1614e78edaa2a9e22d5936aeae5d
without modifications.

As libusb's Android support doesn't work on an unrooted Android phone it needs
some modifcations. Currently libusb_init fails with LIBUSB_ERROR_OTHER (-99).
