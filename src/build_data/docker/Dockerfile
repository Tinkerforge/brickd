FROM alpine AS builder
RUN apk add build-base git pkgconfig linux-headers bash
RUN git clone --depth 1 https://github.com/Tinkerforge/brickd.git
WORKDIR /brickd/src/
RUN git clone --depth 1 https://github.com/Tinkerforge/daemonlib.git
WORKDIR /brickd/src/build_data/linux/libusb_static/
RUN ./compile.sh
WORKDIR /brickd/src/brickd/
RUN make WITH_STATIC=yes WITH_LIBUSB_HOTPLUG_MKNOD=yes

FROM scratch
COPY --from=builder /brickd/src/brickd/brickd /
CMD ["/brickd", "--pid-file", "brickd.pid", "--libusb-hotplug-mknod"]
