This is a special version of libgpiod for brickd for static linking.

Based on libgpiod version 1.6.4 with the libgpiod-brickd.patch applied to it.

Changes:
- Rename libgpiod.a to libgpiod-brickd-static.a to avoid a potential name
  collisions with a system-wide installed libgpiod.a.

To compile libgpiod-brickd-static.a run ./prepare.sh and ./compile.sh.

To modify the libgpiod-brickd.patch run ./prepare.sh then modify the source in
the libgpiod-src directory and run ./capture.sh to update libgpiod-brickd.patch.
