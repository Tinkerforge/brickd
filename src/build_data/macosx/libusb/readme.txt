This is a special version of libusbx for brickd build on Mac OS X 10.6.

Based on libusbx github.org commit 7b62a0a171ac0141a3d12237ab496c49cccd79df
(libusbx version 1.0.17 plus a few fixes) with the libusbx-brickd.patch
applied to it.

Changes:
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
