#
# For GCC/Clang builds on Linux and Mac OS X, and MinGW builds on Windows
#
# Minimum libusb version: 1.0.6
# Tested with libusb: 1.0.6, 1.0.8, 1.0.9, 1.0.16, 1.0.17
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

WITH_LIBUDEV ?= check
WITH_LIBUDEV_DLOPEN ?= no
WITH_LOGGING ?= yes
WITH_EPOLL ?= check
WITH_DEBUG ?= no
WITH_PROFILING ?= no
WITH_USB_REOPEN_ON_SIGUSR1 ?= yes
WITH_PM_UTILS ?= check
WITH_RED_BRICK ?= check

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

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	E := @
endif
endif

ifeq ($(WITH_EPOLL),check)
ifeq ($(PLATFORM),Linux)
	WITH_EPOLL := yes
else
	WITH_EPOLL := no
endif
else
	WITH_EPOLL := no
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_RED_BRICK),check)
ifeq ($(shell hostname),red-brick)
	# Linux and RED Brick
	WITH_RED_BRICK := yes
else
	# Linux, but no RED Brick
	WITH_RED_BRICK := no
endif
endif
else
	# not Linux, no RED Brick
	WITH_RED_BRICK := no
endif

ifneq ($(PLATFORM),Windows)
	PKG_CONFIG := $(shell which pkg-config 2> /dev/null)
ifeq ($(PKG_CONFIG),)
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find pkg-config)
endif
endif
endif

LIBUSB_STATUS := no
LIBUDEV_STATUS := no
PM_UTILS_STATUS := no

ifeq ($(PLATFORM),Windows)
	HOTPLUG := WinAPI
endif

ifeq ($(PLATFORM),Linux)
	HOTPLUG := $(shell pkg-config --exists 'libusb-1.0 >= 1.0.16' && echo libusb || echo libusb \(if supported\))
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
	# not Linux, libudev not available
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
	WITH_PM_UTILS := no
endif

SOURCES_DAEMONLIB := $(call FIX_PATH,../daemonlib/array.c) \
                     $(call FIX_PATH,../daemonlib/base58.c) \
                     $(call FIX_PATH,../daemonlib/config.c) \
                     $(call FIX_PATH,../daemonlib/conf_file.c) \
                     $(call FIX_PATH,../daemonlib/enum.c) \
                     $(call FIX_PATH,../daemonlib/event.c) \
                     $(call FIX_PATH,../daemonlib/io.c) \
                     $(call FIX_PATH,../daemonlib/log.c) \
                     $(call FIX_PATH,../daemonlib/node.c) \
                     $(call FIX_PATH,../daemonlib/packet.c) \
                     $(call FIX_PATH,../daemonlib/queue.c) \
                     $(call FIX_PATH,../daemonlib/socket.c) \
                     $(call FIX_PATH,../daemonlib/utils.c) \
                     $(call FIX_PATH,../daemonlib/writer.c)

SOURCES_BRICKD := base64.c \
                  client.c \
                  config_options.c \
                  hardware.c \
                  hmac.c \
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
	                     $(call FIX_PATH,../daemonlib/socket_winapi.c) \
	                     $(call FIX_PATH,../daemonlib/threads_winapi.c) \
	                     $(call FIX_PATH,../daemonlib/timer_winapi.c)

	SOURCES_BRICKD += event_winapi.c \
	                  fixes_mingw.c \
	                  log_winapi.c \
	                  service.c \
	                  usb_winapi.c
else
	SOURCES_DAEMONLIB += ../daemonlib/daemon.c \
	                     ../daemonlib/log_posix.c \
	                     ../daemonlib/pid_file.c \
	                     ../daemonlib/pipe_posix.c \
	                     ../daemonlib/signal.c \
	                     ../daemonlib/socket_posix.c \
	                     ../daemonlib/threads_posix.c

	SOURCES_BRICKD += usb_posix.c
endif

ifeq ($(PLATFORM),Windows)
	SOURCES_BRICKD += main_windows.c
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_EPOLL),yes)
	SOURCES_DAEMONLIB += ../daemonlib/event_linux.c
else
	SOURCES_DAEMONLIB += ../daemonlib/event_posix.c
endif

	SOURCES_DAEMONLIB += ../daemonlib/timer_linux.c

	SOURCES_BRICKD += main_linux.c
endif

ifeq ($(PLATFORM),Darwin)
	SOURCES_DAEMONLIB += ../daemonlib/event_posix.c \
	                     ../daemonlib/timer_posix.c

	SOURCES_BRICKD += main_macosx.c \
	                  iokit.c
endif

ifeq ($(WITH_LIBUDEV),yes)
	SOURCES_BRICKD += udev.c
endif

ifeq ($(WITH_RED_BRICK),yes)
	SOURCES_BRICKD += file.c \
	                  redapid.c \
	                  red_stack.c \
	                  red_usb_gadget.c \
	                  red_extension.c \
	                  red_rs485_extension.c \
	                  red_ethernet_extension.c

	SOURCES_DAEMONLIB += ../daemonlib/red_gpio.c \
	                     ../daemonlib/red_i2c_eeprom.c \
	                     ../daemonlib/red_led.c
endif

SOURCES := $(SOURCES_DAEMONLIB) $(SOURCES_BRICKD)

OBJECTS := ${SOURCES:.c=.o}
DEPENDS := ${SOURCES:.c=.p}

ifeq ($(PLATFORM),Windows)
	OBJECTS += brickd.o log_messages.o
	TARGET := brickd.exe
	DIST := dist\brickd.exe
else
	TARGET := brickd
	DIST :=
endif

# tell GCC to use C99, but actually write C89 code plus stdint.h and stdbool.h
# to be able to compile it with MSVC/WDK which supports C89 only
CFLAGS += -std=gnu99 -Wall -Wextra -I..

ifeq ($(WITH_DEBUG),yes)
	CFLAGS += -O0 -g -ggdb
else
	CFLAGS += -O2
endif

ifeq ($(PLATFORM),Windows)
	CFLAGS += -DWIN32_LEAN_AND_MEAN -DNDEBUG -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -mconsole -include fixes_mingw.h
	LDFLAGS += -Wl,-subsystem,console
	LIBS += -lws2_32
else
	CFLAGS += -DSYSCONFDIR="\"$(sysconfdir)\"" -DLOCALSTATEDIR="\"$(localstatedir)\""
	LDFLAGS += -pthread
endif

ifeq ($(PLATFORM),Windows)
	LIBUSB_STATUS := 1.0.20 (fork)
	LIBUSB_CFLAGS := -I../build_data/windows/libusb
	LIBUSB_LDFLAGS := -L../build_data/windows/libusb
	LIBUSB_LIBS := -lusb-1.0
endif

ifeq ($(PLATFORM),Linux)
	LIBUSB_VERSION := $(shell pkg-config --exists 'libusb-1.0 >= 1.0.6' && echo okay)
ifeq ($(LIBUSB_VERSION),okay)
	LIBUSB_STATUS := $(shell pkg-config --modversion libusb-1.0)
	LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
	LIBUSB_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libusb-1.0)
	LIBUSB_LIBS := $(shell pkg-config --libs-only-l libusb-1.0)
else
ifneq ($(MAKECMDGOALS),clean)
$(error Could not find libusb-1.0 >= 1.0.6)
endif
endif
endif

ifeq ($(PLATFORM),Darwin)
	LIBUSB_STATUS := 1.0.18 (fork)
	LIBUSB_CFLAGS := -I../build_data/macosx/libusb
	LIBUSB_LDFLAGS := -L../build_data/macosx/libusb
	LIBUSB_LIBS := -lusb-1.0
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
ifeq ($(HOTPLUG),libusb)
	LIBUDEV_STATUS := $(LIBUDEV_STATUS) (unused)
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

ifeq ($(WITH_PROFILING),yes)
	CFLAGS += -DBRICKD_WITH_PROFILING
endif

ifeq ($(WITH_USB_REOPEN_ON_SIGUSR1),yes)
	CFLAGS += -DBRICKD_WITH_USB_REOPEN_ON_SIGUSR1
endif

ifeq ($(WITH_RED_BRICK),yes)
	CFLAGS += -DBRICKD_WITH_RED_BRICK
endif

ifeq ($(PLATFORM),Windows)
	GENERATED := log_messages.h log_messages.rc
endif

ifneq ($(MAKECMDGOALS),clean)
$(info libraries/tools:)
$(info - libusb:    $(LIBUSB_STATUS))
$(info - libudev:   $(LIBUDEV_STATUS))
$(info - pm-utils:  $(PM_UTILS_STATUS))
$(info features:)
$(info - logging:   $(WITH_LOGGING))
$(info - epoll:     $(WITH_EPOLL))
$(info - debug:     $(WITH_DEBUG))
$(info - profiling: $(WITH_PROFILING))
$(info - red-brick: $(WITH_RED_BRICK))
$(info - hotplug:   $(HOTPLUG))
endif

.PHONY: all clean

all: $(DIST) $(TARGET) Makefile

dist\brickd.exe: $(TARGET) Makefile
	$(E)if not exist ..\dist mkdir ..\dist
	@echo CP brickd.exe
	$(E)copy brickd.exe "..\dist\"
	@echo CP libusb-1.0.dll
	$(E)copy "..\build_data\Windows\libusb\libusb-1.0.dll" "..\dist\"

clean: Makefile
	$(E)$(RM) $(GENERATED) $(OBJECTS) $(TARGET) $(DEPENDS)

$(TARGET): $(OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBS)

install: all
ifeq ($(PLATFORM),Linux)
	@echo "MD $(bindir)"
	$(E)$(INSTALL) -d -m 755 $(bindir)

	@echo "MD $(sysconfdir)"
	$(E)$(INSTALL) -d -m 755 $(sysconfdir)

	@echo "MD $(sysconfdir)/init.d"
	$(E)$(INSTALL) -d -m 755 $(sysconfdir)/init.d

	@echo "MD $(sysconfdir)/logrotate.d"
	$(E)$(INSTALL) -d -m 755 $(sysconfdir)/logrotate.d

	@echo "MD $(localstatedir)/log"
	$(E)$(INSTALL) -d -m 755 $(localstatedir)/log

	@echo "MD $(localstatedir)/run"
	$(E)$(INSTALL) -d -m 755 $(localstatedir)/run

	@echo "MD $(datadir)"
	$(E)$(INSTALL) -d -m 755 $(datadir)

	@echo "MD $(datadir)/man"
	$(E)$(INSTALL) -d -m 755 $(datadir)/man

	@echo "MD $(datadir)/man/man8"
	$(E)$(INSTALL) -d -m 755 $(datadir)/man/man8

	@echo "MD $(datadir)/man/man5"
	$(E)$(INSTALL) -d -m 755 $(datadir)/man/man5

	@echo "CP brickd"
	$(E)$(INSTALL) -m 755 brickd $(bindir)

	@echo "CP brickd.conf"
ifeq ($(WITH_RED_BRICK),yes)
	$(E)$(INSTALL) -m 644 ../build_data/linux/etc/brickd-red-brick.conf $(sysconfdir)/brickd.conf
else
	$(E)$(INSTALL) -m 644 ../build_data/linux/etc/brickd-default.conf $(sysconfdir)/brickd.conf
endif

	@echo "CP brickd (init.d script)"
	$(E)$(INSTALL) -m 755 ../build_data/linux/etc/init.d/brickd $(sysconfdir)/init.d

	@echo "CP brickd (logrotate.d script)"
	$(E)$(INSTALL) -m 644 ../build_data/linux/etc/logrotate.d/brickd $(sysconfdir)/logrotate.d

	@echo "CP brickd.8 (manpage)"
	$(E)$(INSTALL) -m 644 ../build_data/linux/usr/share/man/man8/brickd.8 $(datadir)/man/man8
	$(E)gzip -f $(datadir)/man/man8/brickd.8

	@echo "CP brickd.conf.5 (manpage)"
	$(E)$(INSTALL) -m 644 ../build_data/linux/usr/share/man/man5/brickd.conf.5 $(datadir)/man/man5
	$(E)gzip -f $(datadir)/man/man5/brickd.conf.5
endif

ifeq ($(WITH_PM_UTILS),yes)
	@echo "CP 42brickd (pm-utils script)"
	$(E)$(INSTALL) -m 755 ../build_data/linux/usr/lib/pm-utils/sleep.d/42brickd $(shell pkg-config pm-utils --variable pm_sleephooks)
endif

log_messages.h log_messages.rc: log_messages.mc Makefile
	@echo GEN $@
	$(E)windmc -A -b log_messages.mc

%.o: %.c $(GENERATED) Makefile
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
