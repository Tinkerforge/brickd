
# libusb-1.0-0-dev >= 1.0.8
# libudev-dev >= 173
#
# apt-get install libusb-1.0-0-dev libudev-dev

## CONFIG #####################################################################

CC := gcc
RM := rm -f --
PLATFORM := $(shell uname)
WITH_LIBUDEV := check

## RULES ######################################################################

ifeq ($(WITH_LIBUDEV),check)
ifeq ($(PLATFORM),Linux)
	WITH_LIBUDEV := yes
else
	WITH_LIBUDEV := no
endif
endif

SOURCES := brick.c client.c event.c event_posix.c log.c network.c pipe_posix.c \
           socket_posix.c threads_posix.c transfer.c usb.c utils.c

ifeq ($(PLATFORM),Linux)
	SOURCES += main_linux.c
else
	SOURCES += main_macosx.c iokit.c
endif

ifeq ($(WITH_LIBUDEV),yes)
	SOURCES += udev.c
endif

OBJECTS := ${SOURCES:.c=.o}
TARGET := brickd

CFLAGS += -O0 -g -ggdb -Wall -Wextra
LDFLAGS += -pthread

LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
LIBUSB_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libusb-1.0)
LIBUSB_LIBS := $(shell pkg-config --libs-only-l libusb-1.0)

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
	LIBS += -Wl,-framework,IOKit -Wl,-framework,CoreFoundation -lobjc
endif

CFLAGS += -DBRICKD_LOG_ENABLED

.PHONY: all clean

all: $(TARGET)

clean:
	@$(RM) $(OBJECTS) $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "LD   $@"
	@$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBS)

%.o: %.c
	@echo "CC   $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

-include $(subst .c,.d,$(SOURCES))

%.d: %.c
	@$(CC) -M $(CFLAGS) $< > $@.tmp; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.tmp > $@; \
	rm -f $@.tmp
