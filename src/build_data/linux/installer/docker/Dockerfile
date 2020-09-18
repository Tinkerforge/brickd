ARG ARCHITECTURE
FROM ${ARCHITECTURE}/debian:stretch
RUN apt-get update
RUN apt-get install -y apt-utils
RUN apt-get install -y debconf-utils
COPY debconf.conf debconf.conf
RUN debconf-set-selections < debconf.conf
RUN apt-get install -y build-essential git debhelper dh-systemd lintian pkg-config libusb-1.0-0-dev libudev-dev python3 systemd
RUN adduser --disabled-password --gecos '' foobar
USER foobar
