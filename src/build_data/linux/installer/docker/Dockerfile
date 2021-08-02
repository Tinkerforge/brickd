ARG ARCHITECTURE

FROM ${ARCHITECTURE}/debian:stretch

# apt
RUN DEBIAN_FRONTEND=noninteractive apt-get clean
RUN DEBIAN_FRONTEND=noninteractive apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y apt-utils debconf-utils
COPY debconf.conf debconf.conf
RUN debconf-set-selections < debconf.conf

# locales
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y locales locales-all
RUN sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen && locale-gen
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8

# brickd
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential git debhelper dh-systemd lintian pkg-config libusb-1.0-0-dev libudev-dev python3 systemd

# user
RUN adduser --disabled-password --gecos '' foobar
USER foobar
