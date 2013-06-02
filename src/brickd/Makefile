#
# Makefile for GCC builds on Linux and Mac OS X, and MinGW builds on Windows
#
# Debian/Ubuntu:
# sudo apt-get install build-essential libusb-1.0-0-dev libudev-dev
#
# Fedora:
# sudo yum groupinstall "Development Tools"
# sudo yum install libusb1-devel libudev-devel
#

## CONFIG #####################################################################

CC := gcc

WITH_LIBUDEV := check
WITH_LOGGING := yes

## RULES ######################################################################

ifeq ($(OS),Windows_NT)
	PLATFORM := Windows
	RM := del /F /Q
else
	PLATFORM := $(shell uname)
	RM := rm -f --
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

endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	E := @
endif
endif

ifeq ($(PLATFORM),Linux)
ifeq ($(WITH_LIBUDEV),check)
	WITH_LIBUDEV := yes
else
	WITH_LIBUDEV := no
endif
else
	WITH_LIBUDEV := no
endif

SOURCES := brick.c client.c config.c event.c log.c network.c packet.c \
           transfer.c usb.c utils.c

ifeq ($(PLATFORM),Windows)
	SOURCES += event_winapi.c log_winapi.c pipe_winapi.c service.c \
	           socket_winapi.c threads_winapi.c mingwfixes.c
else
	SOURCES += event_posix.c log_posix.c pidfile.c pipe_posix.c socket_posix.c \
	           threads_posix.c
endif

ifeq ($(PLATFORM),Windows)
	SOURCES += main_windows.c
endif

ifeq ($(PLATFORM),Linux)
	SOURCES += main_linux.c
endif

ifeq ($(PLATFORM),Darwin)
	SOURCES += main_macosx.c iokit.c
endif

ifeq ($(WITH_LIBUDEV),yes)
	SOURCES += udev.c
endif

OBJECTS := ${SOURCES:.c=.o}
DEPENDS := ${SOURCES:.c=.d}

ifeq ($(PLATFORM),Windows)
	OBJECTS += brickd.o log_messages.o
	TARGET := brickd.exe
	DIST := dist\brickd.exe
else
	TARGET := brickd
	DIST :=
endif

#CFLAGS += -O0 -g -ggdb
CFLAGS += -O2 -Wall -Wextra

ifeq ($(PLATFORM),Windows)
	CFLAGS += -DWINVER=0x0500 -D_WIN32_WINNT=0x0501 -mconsole -include mingwfixes.h
	LDFLAGS += -Wl,-subsystem,console
	LIBS += -lws2_32
else
	CFLAGS += -DSYSCONFDIR="\"$(sysconfdir)\"" -DLOCALSTATEDIR="\"$(localstatedir)\""
	LDFLAGS += -pthread
endif

ifeq ($(PLATFORM),Windows)
	LIBUSB_CFLAGS := -I../build_data/Windows/libusb
	LIBUSB_LDFLAGS := -L../build_data/Windows/libusb
	LIBUSB_LIBS := -lusb-1.0
else
	LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
	LIBUSB_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libusb-1.0)
	LIBUSB_LIBS := $(shell pkg-config --libs-only-l libusb-1.0)
endif

CFLAGS += $(LIBUSB_CFLAGS)
LDFLAGS += $(LIBUSB_LDFLAGS)
LIBS += $(LIBUSB_LIBS)

ifeq ($(WITH_LIBUDEV),yes)
	LIBUDEV_CFLAGS := $(shell pkg-config --cflags libudev)
	LIBUDEV_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libudev)
	LIBUDEV_LIBS := $(shell pkg-config --libs-only-l libudev)

	CFLAGS += $(LIBUDEV_CFLAGS) -DBRICKD_WITH_LIBUDEV
	LDFLAGS += $(LIBUDEV_LDFLAGS)
	LIBS += $(LIBUDEV_LIBS)
endif

ifeq ($(PLATFORM),Darwin)
	# ensure that there is enough room to rewrite the libusb install name
	LDFLAGS += -Wl,-headerpad_max_install_names
	LIBS += -Wl,-framework,IOKit -Wl,-framework,CoreFoundation -lobjc
endif

ifeq ($(WITH_LOGGING),yes)
	CFLAGS += -DBRICKD_LOG_ENABLED
endif

ifeq ($(PLATFORM),Windows)
	GENERATED := log_messages.h log_messages.rc
endif

.PHONY: all clean

all: $(DIST) $(TARGET) Makefile

dist\brickd.exe: $(TARGET) Makefile
	$(E)if not exist dist mkdir dist
	@echo CP brickd.exe
	$(E)copy brickd.exe "dist\"
	@echo CP libusb-1.0.dll
	$(E)copy "..\build_data\Windows\libusb\libusb-1.0.dll" "dist\"

clean: Makefile
	$(E)$(RM) $(GENERATED) $(OBJECTS) $(TARGET)

clean-depend: Makefile
	$(E)$(RM) $(DEPENDS)

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
	@echo "MD $(localstatedir)/log"
	$(E)$(INSTALL) -d -m 755 $(localstatedir)/log
	@echo "MD $(localstatedir)/run"
	$(E)$(INSTALL) -d -m 755 $(localstatedir)/run

	@echo "CP brickd"
	$(E)$(INSTALL) -m 755 brickd $(bindir)
	@echo "CP brickd.conf"
	$(E)$(INSTALL) -m 644 ../build_data/linux/etc/brickd.conf $(sysconfdir)
	@echo "CP brickd (init script)"
	$(E)$(INSTALL) -m 755 ../build_data/linux/etc/init.d/brickd $(sysconfdir)/init.d
endif

log_messages.h log_messages.rc: log_messages.mc Makefile
	@echo GEN $@
	$(E)windmc -A -b log_messages.mc

%.o: %.c $(GENERATED) Makefile
	@echo CC $@
	$(E)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.rc Makefile
	@echo GEN $@
	$(E)windres $< $@

# FIXME: make dependency handling work for MinGW
ifneq ($(PLATFORM),Windows)
	-include $(subst .c,.d,$(SOURCES))
endif

%.d: %.c Makefile
	$(E)$(CC) -M $(CFLAGS) $< > $@.tmp; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.tmp > $@; \
	rm -f -- $@.tmp
