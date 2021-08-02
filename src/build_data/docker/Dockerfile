FROM alpine AS builder
RUN apk add build-base git pkgconfig linux-headers bash curl autoconf automake libtool
RUN git config --global user.name "foobar"
RUN git config --global user.email "foobar@foobar.com"
RUN mkdir /brickd/
COPY src.zip /brickd/
WORKDIR /brickd/
RUN unzip src.zip
WORKDIR /brickd/src/build_data/linux/libusb/
RUN ./prepare.sh
RUN ./compile.sh
WORKDIR /brickd/src/brickd/
RUN make WITH_STATIC=yes WITH_LIBUSB_HOTPLUG_MKNOD=yes clean
RUN make WITH_STATIC=yes WITH_LIBUSB_HOTPLUG_MKNOD=yes

FROM scratch
COPY --from=builder /brickd/src/brickd/brickd /
CMD ["/brickd", "--pid-file", "brickd.pid", "--libusb-hotplug-mknod"]
