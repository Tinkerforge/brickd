#
# For GCC/Clang builds on Linux and macOS, and MinGW builds on Windows
#
# Minimum libusb version: 1.0.20
#
# Debian/Ubuntu:
# sudo apt-get install build-essential pkg-config libusb-1.0-0-dev pm-utils
#
# Fedora:
# sudo yum groupinstall "Development Tools"
# sudo yum install libusb1-devel pm-utils-devel
#

## CONFIG #####################################################################

CC ?= gcc
WINDRES ?= windres

WITH_HOST ?= check
WITH_TARGET ?= check
WITH_LIBUSB_DLOPEN ?= no
WITH_LIBUSB_STATIC ?= no
WITH_LIBGPIOD_DLOPEN ?= no
WITH_LIBGPIOD_STATIC ?= no
WITH_LOGGING ?= yes
WITH_EPOLL ?= check
WITH_PACKET_TRACE ?= no
WITH_DEBUG ?= no
WITH_STATIC ?= no
WITH_GPROF ?= no
WITH_USB_REOPEN_ON_SIGUSR1 ?= yes
WITH_PM_UTILS ?= check
WITH_SYSTEMD ?= check
WITH_RED_BRICK ?= check
WITH_BRICKLET ?= check
WITH_MESH_SINGLE_ROOT_NODE ?= no
WITH_UNKNOWN_LIBUSB_API_VERSION ?= no
WITH_LIBUSB_HOTPLUG_MKNOD ?= no
WITH_VERSION_SUFFIX ?= no

## RULES ######################################################################

ifeq ($(WITH_HOST),check)
ifeq ($(OS),Windows_NT)
	override WITH_HOST := Windows
else
	override WITH_HOST := $(shell uname)
endif
endif

ifeq ($(WITH_HOST),Windows)
	RM := del /F /Q
	FIX_PATH = $(subst /,\,$1)
else
ifneq ($(WITH_HOST),Linux)
ifneq ($(WITH_HOST),Darwin)
$(error $(WITH_HOST) is not supported as host)
endif
endif

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

	localstatedir_default := /var

ifndef localstatedir
ifeq ($(prefix),/usr)
	localstatedir := /var
else
	localstatedir := $(prefix)/var
endif
endif

ifeq ($(WITH_HOST),Linux)
	runstatedir_default := /run

ifndef runstatedir
ifeq ($(prefix),/usr)
	runstatedir := /run
else
	runstatedir := $(prefix)/run
endif
endif
else
	runstatedir_default := /var/run

ifndef runstatedir
	runstatedir := $(localstatedir)/run
endif
endif

ifndef datadir
	datadir := $(prefix)/share
endif

ifndef libdir
	libdir := $(prefix)/lib
endif
endif

ifeq ($(WITH_TARGET),check)
	override WITH_TARGET := $(WITH_HOST)
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	E := @
endif
endif

ifeq ($(wildcard ../daemonlib),)
$(error Could not find daemonlib. Please symlink or copy daemonlib into the src/ directory)
endif

ifneq ($(WITH_TARGET),Linux)
ifeq ($(WITH_STATIC),yes)
$(error WITH_STATIC is not supported for $(WITH_TARGET))
endif
endif

ifeq ($(WITH_STATIC),yes)
	override WITH_LIBUSB_STATIC := yes
	override WITH_LIBGPIOD_STATIC := yes
endif

ifneq ($(WITH_TARGET),Linux)
ifeq ($(WITH_LIBUSB_DLOPEN),yes)
$(error WITH_LIBUSB_DLOPEN is not supported for $(WITH_TARGET))
endif
endif

ifeq ($(WITH_TARGET),Darwin)
override WITH_LIBUSB_STATIC := yes
endif

ifeq ($(WITH_LIBUSB_DLOPEN),yes)
ifeq ($(WITH_LIBUSB_STATIC),yes)
$(error Cannot dlopen and static link libusb at the same time)
endif
endif

ifneq ($(WITH_TARGET),Linux)
ifeq ($(WITH_LIBGPIOD_DLOPEN),yes)
$(error WITH_LIBGPIOD_DLOPEN is not supported for $(WITH_TARGET))
endif
endif

ifeq ($(WITH_LIBGPIOD_DLOPEN),yes)
ifeq ($(WITH_LIBGPIOD_STATIC),yes)
$(error Cannot dlopen and static link libgpiod at the same time)
endif
endif

ifeq ($(WITH_TARGET),Linux)
ifeq ($(WITH_EPOLL),check)
	override WITH_EPOLL := yes
endif
else
	# not Linux, no epoll
	override WITH_EPOLL := no
endif

ifeq ($(WITH_TARGET),Linux)
ifeq ($(WITH_RED_BRICK),check)
ifneq ($(wildcard /proc/red_brick_uid),)
	# Linux and RED Brick <= 1.9
	override WITH_RED_BRICK := 9
else
ifeq ($(findstring red-brick,$(shell uname -r)),red-brick)
	# Linux and RED Brick > 1.9
	override WITH_RED_BRICK := 10
else
	# Linux, but no RED Brick
	override WITH_RED_BRICK := no
endif
endif
endif
else
	# not Linux, no RED Brick
	override WITH_RED_BRICK := no
endif

ifneq ($(WITH_RED_BRICK),no)
ifeq ($(WITH_VERSION_SUFFIX),no)
	override WITH_VERSION_SUFFIX := +redbrick
else
	override WITH_VERSION_SUFFIX := +redbrick$(WITH_VERSION_SUFFIX)
endif
endif

ifeq ($(WITH_TARGET),Linux)
ifeq ($(WITH_BRICKLET),check)
ifeq ($(WITH_RED_BRICK),no)
	override WITH_BRICKLET := yes
else
	# RED Brick, no Bricklet
	override WITH_BRICKLET := no
endif
endif
else
	# not Linux, no Bricklet
	override WITH_BRICKLET := no
endif

ifeq ($(WITH_TARGET),Linux)
	PKG_CONFIG := $(shell which pkg-config 2>/dev/null)

ifeq ($(PKG_CONFIG),)
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find pkg-config)
endif
endif
endif

PM_UTILS_STATUS := no
SYSTEMD_STATUS := no

ifeq ($(WITH_TARGET),Linux)
	PM_UTILS_EXISTS := $(shell pkg-config --exists pm-utils && echo yes || echo no)

ifeq ($(WITH_PM_UTILS),check)
ifeq ($(PM_UTILS_EXISTS),yes)
	override WITH_PM_UTILS := yes
else
	override WITH_PM_UTILS := no
endif
endif
else
	# not Linux, no pm-utils
	override WITH_PM_UTILS := no
endif

ifeq ($(WITH_TARGET),Linux)
	SYSTEMD_EXISTS := $(shell pkg-config --exists systemd && echo yes || echo no)

ifeq ($(WITH_SYSTEMD),check)
ifeq ($(SYSTEMD_EXISTS),yes)
	override WITH_SYSTEMD := yes
else
	override WITH_SYSTEMD := no
endif
endif
else
	# not Linux, no systemd
	override WITH_SYSTEMD := no
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
                     $(call FIX_PATH,../daemonlib/fifo.c) \
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
                  raspberry_pi.c \
                  sha1.c \
                  stack.c \
                  usb.c \
                  usb_stack.c \
                  usb_transfer.c \
                  websocket.c \
                  zombie.c

ifeq ($(WITH_TARGET),Windows)
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

ifeq ($(WITH_TARGET),Linux)
ifeq ($(WITH_EPOLL),yes)
	SOURCES_DAEMONLIB += ../daemonlib/event_linux.c
else
	SOURCES_DAEMONLIB += ../daemonlib/event_posix.c
endif

	SOURCES_DAEMONLIB += ../daemonlib/gpio_sysfs.c

	SOURCES_BRICKD += main_linux.c
endif

ifeq ($(WITH_TARGET),Darwin)
	SOURCES_DAEMONLIB += ../daemonlib/event_posix.c

	SOURCES_BRICKD += main_macos.c \
	                  iokit.c
endif

ifeq ($(WITH_BRICKLET),yes)
	SOURCES_BRICKD += bricklet.c \
	                  bricklet_stack.c

ifeq ($(WITH_TARGET),Linux)
	SOURCES_BRICKD += bricklet_stack_linux.c \
	                  bricklet_stack_linux_bcm2835.c \
	                  bricklet_stack_linux_spidev.c \
	                  bcm2835.c \
	                  libgpiod2.c \
	                  vcgencmd.c
endif
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
	SOURCES_BRICKD += ../build_data/linux/libusb_dlopen/libusb.c
endif

ifeq ($(WITH_LIBGPIOD_DLOPEN),yes)
	SOURCES_BRICKD += ../build_data/linux/libgpiod_dlopen/gpiod.c
endif

SOURCES := $(SOURCES_DAEMONLIB) $(SOURCES_BRICKD)
OBJECTS := ${SOURCES:.c=.o}
DEPENDS := ${SOURCES:.c=.p}

ifeq ($(WITH_TARGET),Windows)
	OBJECTS += brickd.o
	TARGET := brickd.exe
ifeq ($(WITH_HOST),Windows)
	DIST := ..\dist\brickd.exe
else
	DIST := ../dist/brickd.exe
endif
else
	TARGET := brickd
	DIST :=
endif

ifdef RPM_OPT_FLAGS
	override CFLAGS += $(RPM_OPT_FLAGS)
else
ifeq ($(WITH_DEBUG),yes)
	override CFLAGS += -Og
else
ifeq ($(findstring -O,$(CFLAGS)),)
	override CFLAGS += -O2
endif
endif
endif

ifeq ($(WITH_DEBUG),yes)
	override CFLAGS += -g -ggdb
endif

ifeq ($(WITH_STATIC),yes)
	override LDFLAGS += -static
endif

# tell GCC to use C99, but actually write C89 code plus stdint.h and stdbool.h
# to be able to compile it with MSVC which supports C89 only
override CFLAGS += -std=gnu99 -Wall -Wextra -I.. -include symver.h

ifeq ($(WITH_GPROF),yes)
	override CFLAGS += -pg -no-pie
	override LDFLAGS += -pg -no-pie
endif

ifeq ($(WITH_TARGET),Windows)
	override CFLAGS += -DWIN32_LEAN_AND_MEAN -DNDEBUG -DWINVER=0x0600 -D_WIN32_WINNT=0x0600 -mconsole -include fixes_mingw.h
	override LDFLAGS += -Wl,-subsystem,console
	override LIBS += -lws2_32
else
	override CFLAGS += -DSYSCONFDIR="\"$(sysconfdir)\"" -DLOCALSTATEDIR="\"$(localstatedir)\"" -DRUNSTATEDIR="\"$(runstatedir)\""
	override LDFLAGS += -pthread
endif

ifeq ($(WITH_TARGET),Linux)
	# always link to librt for clock_gettime for compatibility with older glibc.
	# since glibc 2.17 this is not strictly necessary anymore, but detecting glibc
	# version is difficult and fragile, instead always link to librt.
	override LIBS += -lrt
endif

LIBUSB_STATUS := no
HOTPLUG := no

ifeq ($(WITH_TARGET),Windows)
	HOTPLUG := WinAPI
	LIBUSB_VERSION = $(word 1,$(file < ../build_data/windows/libusb/version.txt))

ifeq ($(WITH_LIBUSB_STATIC),yes)
	LIBUSB_STATUS := $(LIBUSB_VERSION) (fork, static)
	LIBUSB_LIBS := -lusb-1.0-brickd-static
else
	LIBUSB_STATUS := $(LIBUSB_VERSION) (fork)
	LIBUSB_LIBS := -lusb-1.0-brickd
endif

	LIBUSB_CFLAGS := -I../build_data/windows/libusb
	LIBUSB_LDFLAGS := -L../build_data/windows/libusb
endif

ifeq ($(WITH_TARGET),Linux)
	HOTPLUG := libusb

ifeq ($(WITH_LIBUSB_DLOPEN),yes)
	LIBUSB_STATUS := >= 1.0.20 (dlopen)
	LIBUSB_CFLAGS := -I../build_data/linux/libusb_dlopen
	LIBUSB_LDFLAGS :=
	LIBUSB_LIBS := -ldl
else
ifeq ($(WITH_LIBUSB_STATIC),yes)
	LIBUSB_STATUS := $(word 1,$(file < ../build_data/linux/libusb/version.txt)) (fork, static)
	LIBUSB_CFLAGS := -I../build_data/linux/libusb
	LIBUSB_LDFLAGS := -L../build_data/linux/libusb
	LIBUSB_LIBS := -lusb-1.0-brickd-static
else
ifeq ($(shell pkg-config --exists 'libusb-1.0 >= 1.0.20' && echo okay),okay)
	LIBUSB_STATUS := $(shell pkg-config --modversion libusb-1.0)
	LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
	LIBUSB_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libusb-1.0)
	LIBUSB_LIBS := $(shell pkg-config --libs-only-l libusb-1.0)
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find libusb-1.0 >= 1.0.20)
endif
endif
endif
endif
endif

ifeq ($(WITH_TARGET),Darwin)
	HOTPLUG := libusb
	LIBUSB_STATUS := $(shell head -n 1 ../build_data/macos/libusb/version.txt) (fork, static)
	LIBUSB_CFLAGS := -I../build_data/macos/libusb
	LIBUSB_LDFLAGS := -L../build_data/macos/libusb
	LIBUSB_LIBS := -lusb-1.0-brickd-static
endif

override CFLAGS += $(LIBUSB_CFLAGS)
override LDFLAGS += $(LIBUSB_LDFLAGS)
override LIBS += $(LIBUSB_LIBS)

LIBGPIOD_STATUS := no

ifeq ($(WITH_TARGET),Linux)
ifeq ($(WITH_BRICKLET),yes)
ifeq ($(WITH_LIBGPIOD_DLOPEN),yes)
	LIBGPIOD_STATUS := >= 1.2 (dlopen)
	LIBGPIOD_CFLAGS := -I../build_data/linux/libgpiod_dlopen
	LIBGPIOD_LDFLAGS :=
	LIBGPIOD_LIBS := -ldl
else
ifeq ($(WITH_LIBGPIOD_STATIC),yes)
	LIBGPIOD_STATUS := $(word 1,$(file < ../build_data/linux/libgpiod/version.txt)) (fork, static)
	LIBGPIOD_CFLAGS := -I../build_data/linux/libgpiod -DBRICKD_LIBGPIOD_ABI_$(word 1,$(file < ../build_data/linux/libgpiod/abi.txt))
	LIBGPIOD_LDFLAGS := -L../build_data/linux/libgpiod
	LIBGPIOD_LIBS := -lgpiod-brickd-static
else
ifeq ($(shell pkg-config --exists 'libgpiod >= 1.2' && echo okay),okay)
	LIBGPIOD_STATUS := $(shell pkg-config --modversion libgpiod)
	LIBGPIOD_CFLAGS := $(shell pkg-config --cflags libgpiod)
	LIBGPIOD_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libgpiod)
	LIBGPIOD_LIBS := $(shell pkg-config --libs-only-l libgpiod)

ifeq ($(shell pkg-config --exists 'libgpiod >= 2' && echo okay),okay)
	LIBGPIOD_CFLAGS += -DBRICKD_LIBGPIOD_ABI_3
else
	LIBGPIOD_CFLAGS += -DBRICKD_LIBGPIOD_ABI_2
endif
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find libgpiod >= 1.2)
endif
endif
endif
endif

	override CFLAGS += $(LIBGPIOD_CFLAGS)
	override LDFLAGS += $(LIBGPIOD_LDFLAGS)
	override LIBS += $(LIBGPIOD_LIBS)
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

ifeq ($(WITH_TARGET),Darwin)
	override CFLAGS += -mmacosx-version-min=10.9
	override LIBS += -Wl,-framework,IOKit -Wl,-framework,CoreFoundation -Wl,-framework,Security -lobjc
endif

ifeq ($(WITH_LOGGING),yes)
	override CFLAGS += -DDAEMONLIB_WITH_LOGGING
endif

ifeq ($(WITH_EPOLL),yes)
	override CFLAGS += -DDAEMONLIB_WITH_EPOLL
endif

ifeq ($(WITH_STATIC),yes)
	override CFLAGS += -DDAEMONLIB_WITH_STATIC
endif

ifeq ($(WITH_PACKET_TRACE),yes)
	override CFLAGS += -DDAEMONLIB_WITH_PACKET_TRACE
endif

ifneq ($(WITH_RED_BRICK),no)
	override CFLAGS += -DDAEMONLIB_WITH_RED_BRICK=${WITH_RED_BRICK}
endif

ifeq ($(WITH_LIBUSB_DLOPEN),yes)
	override CFLAGS += -DBRICKD_WITH_LIBUSB_DLOPEN
endif

ifeq ($(WITH_LIBGPIOD_DLOPEN),yes)
	override CFLAGS += -DBRICKD_WITH_LIBGPIOD_DLOPEN
endif

ifeq ($(WITH_USB_REOPEN_ON_SIGUSR1),yes)
	override CFLAGS += -DBRICKD_WITH_USB_REOPEN_ON_SIGUSR1
endif

ifneq ($(WITH_RED_BRICK),no)
	override CFLAGS += -DBRICKD_WITH_RED_BRICK=${WITH_RED_BRICK}
endif

ifeq ($(WITH_BRICKLET),yes)
	override CFLAGS += -DBRICKD_WITH_BRICKLET
endif

ifeq ($(WITH_MESH_SINGLE_ROOT_NODE),yes)
	override CFLAGS += -DBRICKD_WITH_MESH_SINGLE_ROOT_NODE
endif

ifeq ($(WITH_UNKNOWN_LIBUSB_API_VERSION),yes)
	override CFLAGS += -DBRICKD_WITH_UNKNOWN_LIBUSB_API_VERSION
endif

ifeq ($(WITH_LIBUSB_HOTPLUG_MKNOD),yes)
	override CFLAGS += -DBRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
endif

ifneq ($(WITH_VERSION_SUFFIX),no)
	override CFLAGS += -DBRICKD_VERSION_SUFFIX="\"${WITH_VERSION_SUFFIX}\""
endif

ifneq ($(MAKECMDGOALS),clean)
$(info platform:)
$(info - host:                       $(WITH_HOST))
$(info - target:                     $(WITH_TARGET))
$(info libraries/tools:)
$(info - libusb:                     $(LIBUSB_STATUS))
$(info - libgpiod:                   $(LIBGPIOD_STATUS))
$(info - pm-utils:                   $(PM_UTILS_STATUS))
$(info - systemd:                    $(SYSTEMD_STATUS))
$(info features:)
$(info - logging:                    $(WITH_LOGGING))
$(info - epoll:                      $(WITH_EPOLL))
$(info - packet-trace:               $(WITH_PACKET_TRACE))
$(info - debug:                      $(WITH_DEBUG))
$(info - static:                     $(WITH_STATIC))
$(info - gprof:                      $(WITH_GPROF))
$(info - red-brick:                  $(WITH_RED_BRICK))
$(info - bricklet:                   $(WITH_BRICKLET))
$(info - unknown-libusb-api-version: $(WITH_UNKNOWN_LIBUSB_API_VERSION))
$(info - mesh-single-root-node:      $(WITH_MESH_SINGLE_ROOT_NODE))
$(info - libusb-hotplug-mknod:       $(WITH_LIBUSB_HOTPLUG_MKNOD))
$(info - version-suffix:             $(WITH_VERSION_SUFFIX))
$(info - hotplug:                    $(HOTPLUG))
$(info options:)
$(info - CFLAGS:                     $(CFLAGS))
$(info - LDFLAGS:                    $(LDFLAGS))
$(info - LIBS:                       $(LIBS))
endif

.PHONY: all clean install

all: $(DIST) $(TARGET) Makefile

..\dist\brickd.exe: $(TARGET) Makefile
	$(E)if not exist ..\dist\ mkdir ..\dist\
	@echo CP brickd.exe
	$(E)copy "brickd.exe" "..\dist\"
ifeq ($(WITH_LIBUSB_STATIC),no)
	@echo CP libusb-1.0-brickd.dll
	$(E)copy "..\build_data\windows\libusb\libusb-1.0-brickd.dll" "..\dist\"
endif

../dist/brickd.exe: $(TARGET) Makefile
	$(E)mkdir -p ../dist/
	@echo CP brickd.exe
	$(E)cp brickd.exe ../dist/
ifeq ($(WITH_LIBUSB_STATIC),no)
	@echo CP libusb-1.0-brickd.dll
	$(E)cp ../build_data/windows/libusb/libusb-1.0-brickd.dll ../dist/
endif

clean: Makefile
	$(E)$(RM) $(OBJECTS) $(TARGET) $(DEPENDS)

$(TARGET): $(OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBS)

install: all
ifeq ($(WITH_TARGET),Linux)
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

ifneq ($(localstatedir),$(localstatedir_default))
	@echo MD $(localstatedir)/log
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log
endif

ifneq ($(runstatedir),$(runstatedir_default))
	@echo MD $(runstatedir)
	$(E)$(INSTALL) -d -m 755 $(DESTDIR)$(runstatedir)
endif

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
	$(E)$(INSTALL) -m 755 ../build_data/linux/installer/etc/init.d/brickd $(DESTDIR)$(sysconfdir)/init.d/brickd
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
ifneq ($(WITH_TARGET),Windows)
	$(E)$(CC) -M $(CFLAGS) -o $*.d $<; cp $*.d $*.p; sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.p; rm -f $*.d
endif
	$(E)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.rc Makefile
	@echo GEN $@
	$(E)$(WINDRES) $< $@

# FIXME: make dependency handling work for MinGW
# http://make.paulandlesley.org/autodep.html
ifneq ($(WITH_TARGET),Windows)
-include $(DEPENDS)
endif
