#
# Makefile for GCC builds on Linux and Mac OS X, and MinGW builds on Windows
#
# Debian/Ubuntu:
# sudo apt-get install build-essential
#
# Fedora:
# sudo yum groupinstall "Development Tools"
#

## CONFIG #####################################################################

CC ?= gcc

## RULES ######################################################################

ifeq ($(OS),Windows_NT)
	PLATFORM := Windows
	RM := del /F /Q
	FIX_PATH = $(subst /,\,$1)
else
	PLATFORM := $(shell uname)
	RM := rm -f --
	FIX_PATH = $1
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	E := @
endif
endif

SOURCES := throughput_test.c ip_connection.c brick_master.c $(call FIX_PATH,../brickd/utils.c)

ifeq ($(PLATFORM),Windows)
	SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
endif

OBJECTS := ${SOURCES:.c=.o}
DEPENDS := ${SOURCES:.c=.d}

ifeq ($(PLATFORM),Windows)
	TARGET := throughput_test.exe
else
	TARGET := throughput_test
endif

CFLAGS += -O2 -Wall -Wextra
#CFLAGS += -O0 -g -ggdb

ifeq ($(PLATFORM),Windows)
	CFLAGS += -DWINVER=0x0500 -D_WIN32_WINNT=0x0501 -mconsole -include ../brickd/fixes_mingw.h
	LDFLAGS += -Wl,-subsystem,console
	LIBS += -lws2_32
else
	LDFLAGS += -pthread
endif

.PHONY: all clean

all: $(TARGET) Makefile

clean: Makefile
	$(E)$(RM) $(GENERATED) $(OBJECTS) $(TARGET)

clean-depend: Makefile
	$(E)$(RM) $(DEPENDS)

$(TARGET): $(OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBS)

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
