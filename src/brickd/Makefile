#
# For GCC/Clang builds on Linux and macOS, and MinGW builds on Windows
#
# Minimum libusb version: 1.0.6
# Tested with libusb: 1.0.6, 1.0.8, 1.0.9, 1.0.16, 1.0.17, 1.0.19, 1.0.21
#
# Debian/Ubuntu:
# sudo apt-get install build-essential pkg-config libusb-1.0-0-dev libudev-dev pm-utils
#
# Fedora:
# sudo yum groupinstall "Development Tools"
# sudo yum install libusb1-devel libudev-devel pm-utils-devel
#

## CONFIG #####################################################################

CC ?= gcc

WITH_LIBUSB_DLOPEN ?= no
WITH_LIBUSB_STATIC ?= no
WITH_LIBUDEV ?= check
WITH_LIBUDEV_DLOPEN ?= no
WITH_LOGGING ?= yes
WITH_EPOLL ?= check
WITH_PACKET_TRACE ?= no
WITH_DEBUG ?= no
WITH_GPROF ?= no
WITH_USB_REOPEN_ON_SIGUSR1 ?= yes
WITH_PM_UTILS ?= check
WITH_SYSTEMD ?= check
WITH_RED_BRICK ?= check
WITH_BRICKLET ?= check
WITH_MESH_SINGLE_ROOT_NODE ?= no
WITH_UNKNOWN_LIBUSB_API_VERSION ?= no

## RULES ######################################################################

ifeq ($(OS),Windows_NT)
	PLATFORM := Windows
	RM := del /F /Q
	FIX_PATH = $(subst /,\,$1)
else
	PLATFORM := $(shell uname)
	RM := rm -f --
	FIX_PATH = $1
	INSTALL := install

ifndef prefix
	prefix := /usr
endif

ifndef bindir
	bindir := $(prefix)/bin
endif

ifndef sysconfdir
ifeq ($(prefix),/usr)
	sysconfdir := /etc
else
	sysconfdir := $(prefix)/etc
endif
endif

ifndef localstatedir
ifeq ($(prefix),/usr)
	localstatedir := /var
else
	localstatedir := $(prefix)/var
endif
endif

ifndef datadir
	datadir := $(prefix)/share
endif
endif

ifndef libdir
	libdir := $(prefix)/lib
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	E := @
endif
endif

ifeq ($(wildcard ../daemonlib),)
$(error Could not find daemonlib. Please symlink or copy daemonlib into the src/ directory)
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_EPOLL),check)
	WITH_EPOLL := yes
endif
else
	# not Linux, no epoll
	WITH_EPOLL := no
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_RED_BRICK),check)
ifneq ($(wildcard /proc/red_brick_uid),)
	# Linux and RED Brick <= 1.9
	WITH_RED_BRICK := 9
else
ifeq ($(findstring red-brick,$(shell uname -r)),red-brick)
	# Linux and RED Brick > 1.9
	WITH_RED_BRICK := 10
else
	# Linux, but no RED Brick
	WITH_RED_BRICK := no
endif
endif
endif
else
	# not Linux, no RED Brick
	WITH_RED_BRICK := no
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_BRICKLET),check)
ifeq ($(WITH_RED_BRICK),no)
	WITH_BRICKLET := yes
else
	# RED Brick, no Bricklet
	WITH_BRICKLET := no
endif
endif
else
	# not Linux, no Bricklet
	WITH_BRICKLET := no
endif

ifeq ($(PLATFORM),Linux)
	PKG_CONFIG := $(shell which pkg-config 2>/dev/null)

ifeq ($(PKG_CONFIG),)
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find pkg-config)
endif
endif
endif

LIBUSB_STATUS := no
LIBUDEV_STATUS := no
HOTPLUG := no

ifeq ($(PLATFORM),Windows)
	HOTPLUG := WinAPI
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_LIBUSB_DLOPEN),yes)
ifeq ($(WITH_LIBUSB_STATIC),yes)
$(error Cannot use dlopen and static linking for libusb)
endif

	HOTPLUG := libusb (if supported)
else
	HOTPLUG := $(shell pkg-config --exists 'libusb-1.0 >= 1.0.16' && echo libusb || echo libusb \(if supported\))
endif

ifeq ($(WITH_LIBUDEV),check)
ifeq ($(HOTPLUG),libusb)
	# libusb hotplug, libudev not required
	WITH_LIBUDEV := no
	LIBUDEV_STATUS := not required
else
	# no libusb hotplug, use libudev instead
	WITH_LIBUDEV := yes
endif
endif
else
	# not Linux, no libudev
	WITH_LIBUDEV := no
endif

ifeq ($(WITH_LIBUDEV),yes)
ifeq ($(HOTPLUG),libusb (if supported))
	HOTPLUG := libusb (if supported) or libudev
endif
endif

ifeq ($(PLATFORM),Darwin)
	HOTPLUG := libusb
endif

PM_UTILS_STATUS := no
SYSTEMD_STATUS := no

ifeq ($(PLATFORM),Linux)
	PM_UTILS_EXISTS := $(shell pkg-config --exists pm-utils && echo yes || echo no)

ifeq ($(WITH_PM_UTILS),check)
ifeq ($(PM_UTILS_EXISTS),yes)
	WITH_PM_UTILS := yes
else
	WITH_PM_UTILS := no
endif
endif
else
	# not Linux, no pm-utils
	WITH_PM_UTILS := no
endif

ifeq ($(PLATFORM),Linux)
	SYSTEMD_EXISTS := $(shell pkg-config --exists systemd && echo yes || echo no)

ifeq ($(WITH_SYSTEMD),check)
ifeq ($(SYSTEMD_EXISTS),yes)
	WITH_SYSTEMD := yes
else
	WITH_SYSTEMD := no
endif
endif
else
	# not Linux, no systemd
	WITH_SYSTEMD := no
endif

ifeq ($(WITH_SYSTEMD),yes)
	SYSTEMD_SYSTEM_UNIT_DIR := $(shell pkg-config --variable=systemdsystemunitdir systemd)
endif

SOURCES_DAEMONLIB := $(call FIX_PATH,../daemonlib/array.c) \
                     $(call FIX_PATH,../daemonlib/base58.c) \
                     $(call FIX_PATH,../daemonlib/config.c) \
                     $(call FIX_PATH,../daemonlib/conf_file.c) \
                     $(call FIX_PATH,../daemonlib/enum.c) \
                     $(call FIX_PATH,../daemonlib/event.c) \
                     $(call FIX_PATH,../daemonlib/file.c) \
                     $(call FIX_PATH,../daemonlib/io.c) \
                     $(call FIX_PATH,../daemonlib/log.c) \
                     $(call FIX_PATH,../daemonlib/node.c) \
                     $(call FIX_PATH,../daemonlib/packet.c) \
                     $(call FIX_PATH,../daemonlib/pearson_hash.c) \
                     $(call FIX_PATH,../daemonlib/queue.c) \
                     $(call FIX_PATH,../daemonlib/ringbuffer.c) \
                     $(call FIX_PATH,../daemonlib/socket.c) \
                     $(call FIX_PATH,../daemonlib/threads.c) \
                     $(call FIX_PATH,../daemonlib/timer.c) \
                     $(call FIX_PATH,../daemonlib/utils.c) \
                     $(call FIX_PATH,../daemonlib/writer.c)

SOURCES_BRICKD := base64.c \
                  client.c \
                  config_options.c \
                  hardware.c \
                  hmac.c \
                  mesh.c \
                  mesh_packet.c \
                  mesh_stack.c \
                  network.c \
                  sha1.c \
                  stack.c \
                  usb.c \
                  usb_stack.c \
                  usb_transfer.c \
                  websocket.c \
                  zombie.c

ifeq ($(PLATFORM),Windows)
	SOURCES_DAEMONLIB += $(call FIX_PATH,../daemonlib/pipe_winapi.c) \
	                     $(call FIX_PATH,../daemonlib/socket_winapi.c)

	SOURCES_BRICKD += event_winapi.c \
	                  fixes_mingw.c \
	                  log_winapi.c \
	                  main_winapi.c \
	                  service.c \
	                  usb_winapi.c \
	                  usb_windows.c
else
	SOURCES_DAEMONLIB += ../daemonlib/daemon.c \
	                     ../daemonlib/log_posix.c \
	                     ../daemonlib/pid_file.c \
	                     ../daemonlib/pipe_posix.c \
	                     ../daemonlib/signal.c \
	                     ../daemonlib/socket_posix.c

	SOURCES_BRICKD += usb_posix.c
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_EPOLL),yes)
	SOURCES_DAEMONLIB += ../daemonlib/event_linux.c
else
	SOURCES_DAEMONLIB += ../daemonlib/event_posix.c
endif

	SOURCES_DAEMONLIB += ../daemonlib/gpio_sysfs.c

	SOURCES_BRICKD += main_linux.c
endif

ifeq ($(PLATFORM),Darwin)
	SOURCES_DAEMONLIB += ../daemonlib/event_posix.c

	SOURCES_BRICKD += main_macos.c \
	                  iokit.c
endif

ifeq ($(WITH_LIBUDEV),yes)
	SOURCES_BRICKD += udev.c
endif

ifeq ($(WITH_BRICKLET),yes)
	SOURCES_BRICKD += bricklet.c \
	                  bricklet_stack.c
endif

ifneq ($(WITH_RED_BRICK),no)
	SOURCES_BRICKD += redapid.c \
	                  red_stack.c \
	                  red_usb_gadget.c \
	                  red_extension.c \
	                  red_rs485_extension.c \
	                  red_ethernet_extension.c

	SOURCES_DAEMONLIB += ../daemonlib/gpio_red.c \
	                     ../daemonlib/red_i2c_eeprom.c \
	                     ../daemonlib/red_led.c
endif

ifeq ($(WITH_LIBUSB_DLOPEN),yes)
	SOURCES_BRICKD += ../build_data/linux/libusb/libusb.c
endif

SOURCES := $(SOURCES_DAEMONLIB) $(SOURCES_BRICKD)
OBJECTS := ${SOURCES:.c=.o}
DEPENDS := ${SOURCES:.c=.p}

ifeq ($(PLATFORM),Windows)
	OBJECTS += brickd.o
	TARGET := brickd.exe
	DIST := ..\dist\brickd.exe
else
	TARGET := brickd
	DIST :=
endif

ifdef RPM_OPT_FLAGS
	CFLAGS = $(RPM_OPT_FLAGS)
else
ifeq ($(WITH_DEBUG),yes)
	CFLAGS = -O0 -g -ggdb
else
	CFLAGS = -O2
endif
endif

# tell GCC to use C99, but actually write C89 code plus stdint.h and stdbool.h
# to be able to compile it with MSVC/WDK which supports C89 only
CFLAGS += -std=gnu99 -Wall -Wextra -I..

ifeq ($(WITH_GPROF),yes)
	CFLAGS += -pg -no-pie
	LDFLAGS += -pg -no-pie
endif

ifeq ($(PLATFORM),Windows)
	CFLAGS += -DWIN32_LEAN_AND_MEAN -DNDEBUG -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -mconsole -include fixes_mingw.h
	LDFLAGS += -Wl,-subsystem,console
	LIBS += -lws2_32
else
	CFLAGS += -DSYSCONFDIR="\"$(sysconfdir)\"" -DLOCALSTATEDIR="\"$(localstatedir)\""
	LDFLAGS += -pthread
endif

ifeq ($(PLATFORM),Linux)
	# always link to librt for clock_gettime for compatibility with older glibc.
	# since glibc 2.17 this is not strictly necessary anymore, but detecting glibc
	# version is difficult and fragile, instead always link to librt.
	LIBS += -lrt
endif

ifeq ($(PLATFORM),Windows)
	LIBUSB_STATUS := 1.0.23 (fork)
	LIBUSB_CFLAGS := -I../build_data/windows/libusb
	LIBUSB_LDFLAGS := -L../build_data/windows/libusb
	LIBUSB_LIBS := -lusb-1.0
endif

LIBUDEV_LINK := no

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_LIBUSB_DLOPEN),yes)
	LIBUSB_STATUS := dlopen
	LIBUSB_CFLAGS := -I../build_data/linux/libusb
	LIBUSB_LDFLAGS :=
	LIBUSB_LIBS :=
else
	LIBUSB_VERSION := $(shell pkg-config --exists 'libusb-1.0 >= 1.0.6' && echo okay)

ifeq ($(LIBUSB_VERSION),okay)
	LIBUSB_STATUS := $(shell pkg-config --modversion libusb-1.0)
	LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
	LIBUSB_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libusb-1.0)

ifeq ($(WITH_LIBUSB_STATIC),yes)
	LIBUSB_STATUS := $(LIBUSB_STATUS) (static)
	LIBUSB_LIBS := -Wl,-Bstatic $(shell pkg-config --libs-only-l libusb-1.0) -Wl,-Bdynamic

ifeq ($(findstring -ludev,$(shell pkg-config --libs-only-l --static libusb-1.0)),-ludev)
	LIBUDEV_LINK := yes
endif
else
	LIBUSB_LIBS := $(shell pkg-config --libs-only-l libusb-1.0)
endif
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find libusb-1.0 >= 1.0.6)
endif
endif
endif
endif

ifeq ($(PLATFORM),Darwin)
	LIBUSB_STATUS := 1.0.23 (fork)
	LIBUSB_CFLAGS := -I../build_data/macos/libusb
	LIBUSB_LDFLAGS := -L../build_data/macos/libusb
	LIBUSB_LIBS := -lusb-1.0-brickd
endif

CFLAGS += $(LIBUSB_CFLAGS)
LDFLAGS += $(LIBUSB_LDFLAGS)
LIBS += $(LIBUSB_LIBS)

ifeq ($(WITH_LIBUDEV),yes)
	CFLAGS += -DBRICKD_WITH_LIBUDEV

ifeq ($(WITH_LIBUDEV_DLOPEN),yes)
	LIBUDEV_STATUS := dlopen
	CFLAGS += -DBRICKD_WITH_LIBUDEV_DLOPEN
else
	LIBUDEV_LINK := yes
endif

ifeq ($(HOTPLUG),libusb)
	LIBUDEV_STATUS := $(LIBUDEV_STATUS) (unused)
endif
endif

ifeq ($(LIBUDEV_LINK),yes)
	LIBUDEV_EXISTS := $(shell pkg-config --exists libudev && echo yes || echo no)

ifeq ($(LIBUDEV_EXISTS),yes)
	LIBUDEV_STATUS := $(shell pkg-config --modversion libudev)
	LIBUDEV_CFLAGS := $(shell pkg-config --cflags libudev)
	LIBUDEV_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libudev)
	LIBUDEV_LIBS := $(shell pkg-config --libs-only-l libudev)
	CFLAGS += $(LIBUDEV_CFLAGS)
	LDFLAGS += $(LIBUDEV_LDFLAGS)
	LIBS += $(LIBUDEV_LIBS)
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find libudev)
endif
endif
endif

ifeq ($(WITH_PM_UTILS),yes)
	PM_UTILS_EXISTS := $(shell pkg-config --exists pm-utils && echo yes || echo no)

ifeq ($(PM_UTILS_EXISTS),yes)
	PM_UTILS_STATUS := $(shell pkg-config --modversion pm-utils)
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find pm-utils)
endif
endif
endif

ifeq ($(WITH_SYSTEMD),yes)
ifeq ($(SYSTEMD_EXISTS),yes)
	SYSTEMD_STATUS := $(shell systemctl --version | head -n1 | awk '{{ print $$2 }}')
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find systemd)
endif
endif
endif

ifneq ($(PLATFORM),Windows)
	LIBS += -ldl
endif

ifeq ($(PLATFORM),Darwin)
	# ensure that there is enough room to rewrite the libusb install name
	LDFLAGS += -Wl,-headerpad_max_install_names
	LIBS += -Wl,-framework,IOKit -Wl,-framework,CoreFoundation -lobjc
endif

ifeq ($(WITH_LOGGING),yes)
	CFLAGS += -DDAEMONLIB_WITH_LOGGING
endif

ifeq ($(WITH_EPOLL),yes)
	CFLAGS += -DDAEMONLIB_WITH_EPOLL
endif

ifeq ($(WITH_PACKET_TRACE),yes)
	CFLAGS += -DDAEMONLIB_WITH_PACKET_TRACE
endif

ifneq ($(WITH_RED_BRICK),no)
	CFLAGS += -DDAEMONLIB_WITH_RED_BRICK=${WITH_RED_BRICK}
endif

ifeq ($(WITH_LIBUSB_DLOPEN),yes)
	CFLAGS += -DBRICKD_WITH_LIBUSB_DLOPEN
endif

ifeq ($(WITH_USB_REOPEN_ON_SIGUSR1),yes)
	CFLAGS += -DBRICKD_WITH_USB_REOPEN_ON_SIGUSR1
endif

ifneq ($(WITH_RED_BRICK),no)
	CFLAGS += -DBRICKD_WITH_RED_BRICK=${WITH_RED_BRICK}
endif

ifeq ($(WITH_BRICKLET),yes)
	CFLAGS += -DBRICKD_WITH_BRICKLET
endif

ifeq ($(WITH_MESH_SINGLE_ROOT_NODE),yes)
	CFLAGS += -DBRICKD_WITH_MESH_SINGLE_ROOT_NODE
endif

ifeq ($(WITH_UNKNOWN_LIBUSB_API_VERSION),yes)
	CFLAGS += -DBRICKD_UNKNOWN_LIBUSB_API_VERSION
endif

ifneq ($(MAKECMDGOALS),clean)
$(info libraries/tools:)
$(info - libusb:                     $(LIBUSB_STATUS))
$(info - libudev:                    $(LIBUDEV_STATUS))
$(info - pm-utils:                   $(PM_UTILS_STATUS))
$(info - systemd:                    $(SYSTEMD_STATUS))
$(info features:)
$(info - logging:                    $(WITH_LOGGING))
$(info - epoll:                      $(WITH_EPOLL))
$(info - packet-trace:               $(WITH_PACKET_TRACE))
$(info - debug:                      $(WITH_DEBUG))
$(info - gprof:                      $(WITH_GPROF))
$(info - red-brick:                  $(WITH_RED_BRICK))
$(info - bricklet:                   $(WITH_BRICKLET))
$(info - unknown-libusb-api-version: $(WITH_UNKNOWN_LIBUSB_API_VERSION))
$(info - mesh-single-root-node:      $(WITH_MESH_SINGLE_ROOT_NODE))
$(info - hotplug:                    $(HOTPLUG))
$(info options:)
$(info - CFLAGS:                     $(CFLAGS))
$(info - LDFLAGS:                    $(LDFLAGS))
$(info - LIBS:                       $(LIBS))
endif

.PHONY: all clean

all: $(DIST) $(TARGET) Makefile

..\dist\brickd.exe: $(TARGET) Makefile
	$(E)if not exist ..\dist mkdir ..\dist
	@echo CP brickd.exe
	$(E)copy "brickd.exe" "..\dist\"
	@echo CP libusb-1.0.dll
	$(E)copy "..\build_data\windows\libusb\libusb-1.0.dll" "..\dist\"

clean: Makefile
	$(E)$(RM) $(OBJECTS) $(TARGET) $(DEPENDS)

$(TARGET): $(OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBS)

install: all
ifeq ($(PLATFORM),Linux)
	@echo MD $(bindir)
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(bindir)

	@echo MD $(sysconfdir)
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(sysconfdir)

ifeq ($(WITH_SYSTEMD),no)
	@echo MD $(sysconfdir)/init.d
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(sysconfdir)/init.d
endif

	@echo MD $(sysconfdir)/logrotate.d
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(sysconfdir)/logrotate.d

	@echo MD $(localstatedir)/log
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log

	@echo MD $(localstatedir)/run
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run

	@echo MD $(datadir)
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(datadir)

	@echo MD $(datadir)/man/man8
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(datadir)/man/man8

	@echo MD $(datadir)/man/man5
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(datadir)/man/man5

ifeq ($(WITH_PM_UTILS),yes)
	@echo MD $(libdir)/pm-utils/power.d
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(libdir)/pm-utils/power.d

	@echo MD $(libdir)/pm-utils/sleep.d
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(libdir)/pm-utils/sleep.d
endif

ifeq ($(WITH_SYSTEMD),yes)
	@echo MD $(SYSTEMD_SYSTEM_UNIT_DIR)
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(SYSTEMD_SYSTEM_UNIT_DIR)
endif

	@echo CP brickd
	$(E)$(INSTALL) -m 755 brickd $(DESTDIR)$(bindir)

	@echo CP brickd.conf
ifneq ($(WITH_RED_BRICK),no)
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/etc/brickd-red-brick.conf $(DESTDIR)$(sysconfdir)/brickd.conf
else
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/etc/brickd-default.conf $(DESTDIR)$(sysconfdir)/brickd.conf
endif

ifeq ($(WITH_SYSTEMD),no)
	@echo CP brickd [init.d script]
	$(E)$(INSTALL) -m 755 ../build_data/linux/installer/usr/share/brickd/brickd-sysv-init $(DESTDIR)$(sysconfdir)/init.d/brickd
else
	@echo CP brickd.service [systemd service]
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/lib/systemd/system/brickd.service $(DESTDIR)$(SYSTEMD_SYSTEM_UNIT_DIR)
endif

	@echo CP brickd [logrotate.d script]
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/etc/logrotate.d/brickd $(DESTDIR)$(sysconfdir)/logrotate.d

	@echo CP brickd.8 [manpage]
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/usr/share/man/man8/brickd.8 $(DESTDIR)$(datadir)/man/man8
	$(E)gzip -n -f $(DESTDIR)$(datadir)/man/man8/brickd.8

	@echo CP brickd.conf.5 [manpage]
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/usr/share/man/man5/brickd.conf.5 $(DESTDIR)$(datadir)/man/man5
	$(E)gzip -n -f $(DESTDIR)$(datadir)/man/man5/brickd.conf.5
endif

ifeq ($(WITH_PM_UTILS),yes)
	@echo CP 42brickd [pm-utils script]
	$(E)$(INSTALL) -m 755 ../build_data/linux/installer/usr/lib/pm-utils/power.d/42brickd $(DESTDIR)$(libdir)/pm-utils/power.d
	$(E)$(INSTALL) -m 755 ../build_data/linux/installer/usr/lib/pm-utils/sleep.d/42brickd $(DESTDIR)$(libdir)/pm-utils/sleep.d
endif

ifeq ($(WITH_SYSTEMD),yes)
	@echo CP brickd-resume.service [systemd service]
	$(E)$(INSTALL) -m 644 ../build_data/linux/installer/lib/systemd/system/brickd-resume.service $(DESTDIR)$(SYSTEMD_SYSTEM_UNIT_DIR)
endif

%.o: %.c Makefile
	@echo CC $@
ifneq ($(PLATFORM),Windows)
	$(E)$(CC) -M $(CFLAGS) -o $*.d $<; cp $*.d $*.p; sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.p; rm -f $*.d
endif
	$(E)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.rc Makefile
	@echo GEN $@
	$(E)windres $< $@

# FIXME: make dependency handling work for MinGW
# http://make.paulandlesley.org/autodep.html
ifneq ($(PLATFORM),Windows)
-include $(DEPENDS)
endif
