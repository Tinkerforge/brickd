
# libusb-1.0-0-dev >= 1.0.8
# libudev-dev >= 173
#
# apt-get install libusb-1.0-0-dev libudev-dev

CC := gcc
RM := rm -f --

SOURCES := brick.c event.c event_posix.c main_linux.c log.c network.c \
           pipe_posix.c socket_posix.c threads_posix.c transfer.c udev.c \
           usb.c utils.c
OBJECTS := ${SOURCES:.c=.o}
TARGET := brickd

CFLAGS += -O0 -g -ggdb -Wall -Wextra
LDFLAGS += -pthread

LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0)
LIBUSB_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libusb-1.0)
LIBUSB_LIBS := $(shell pkg-config --libs-only-l libusb-1.0)

LIBUDEV_CFLAGS := $(shell pkg-config --cflags libudev)
LIBUDEV_LDFLAGS := $(shell pkg-config --libs-only-other --libs-only-L libudev)
LIBUDEV_LIBS := $(shell pkg-config --libs-only-l libudev)

CFLAGS += $(LIBUSB_CFLAGS) $(LIBUDEV_CFLAGS)
LDFLAGS += $(LIBUSB_LDFLAGS) $(LIBUDEV_LDFLAGS)

.PHONY: all clean

all: $(TARGET)

clean:
	@$(RM) $(OBJECTS) $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "LD   $@"
	@$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBUSB_LIBS) $(LIBUDEV_LIBS)

%.o: %.c
	@echo "CC   $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

-include $(subst .c,.d,$(SOURCES))

%.d: %.c
	@$(CC) -M $(CFLAGS) $< > $@.tmp; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.tmp > $@; \
	rm -f $@.tmp
