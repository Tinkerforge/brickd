special version of libusbx for brickd

based on libusbx github.org commit 66db81a41cd64e638795f1da117e912e14d76f7c
with the libusbx-brickd.patch applied to it

known issues:
- Windows XP: submitted transfers are not correctly aborted on USB device
  disconnect. this results in leaking the underlying fake file descriptor.
  currently libsubx has a hard limit of 1024 fake file descriptors. when
  libusbx runs out of fake file descriptors then new transfers cannot be
  created anymore. workaround: brickd restart
