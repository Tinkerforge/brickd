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

ARRAY_TEST_SOURCES := array_test.c $(call FIX_PATH,../brickd/array.c)
QUEUE_TEST_SOURCES := queue_test.c $(call FIX_PATH,../brickd/queue.c)
THROUGHPUT_TEST_SOURCES := throughput_test.c ip_connection.c brick_master.c $(call FIX_PATH,../brickd/utils.c)

SOURCES := $(ARRAY_TEST_SOURCES) $(QUEUE_TEST_SOURCES) $(THROUGHPUT_TEST_SOURCES)

ifeq ($(PLATFORM),Windows)
	ARRAY_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	QUEUE_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	THROUGHPUT_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
endif

ARRAY_TEST_OBJECTS := ${ARRAY_TEST_SOURCES:.c=.test.o}
QUEUE_TEST_OBJECTS := ${QUEUE_TEST_SOURCES:.c=.test.o}
THROUGHPUT_TEST_OBJECTS := ${THROUGHPUT_TEST_SOURCES:.c=.test.o}

OBJECTS := $(ARRAY_TEST_OBJECTS) $(QUEUE_TEST_OBJECTS) $(THROUGHPUT_TEST_OBJECTS)
DEPENDS := ${ARRAY_TEST_SOURCES:.c=.test.d} ${QUEUE_TEST_SOURCES:.c=.test.d} ${THROUGHPUT_TEST_SOURCES:.c=.test.d}

ifeq ($(PLATFORM),Windows)
	ARRAY_TEST_TARGET := array_test.exe
	QUEUE_TEST_TARGET := queue_test.exe
	THROUGHPUT_TEST_TARGET := throughput_test.exe
else
	ARRAY_TEST_TARGET := array_test
	QUEUE_TEST_TARGET := queue_test
	THROUGHPUT_TEST_TARGET := throughput_test
endif

TARGETS := $(ARRAY_TEST_TARGET) $(QUEUE_TEST_TARGET) $(THROUGHPUT_TEST_TARGET)

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

all: $(TARGETS) Makefile

clean: Makefile
	$(E)$(RM) $(GENERATED) $(OBJECTS) $(TARGETS)

clean-depend: Makefile
	$(E)$(RM) $(DEPENDS)

$(ARRAY_TEST_TARGET): $(ARRAY_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(ARRAY_TEST_TARGET) $(LDFLAGS) $(ARRAY_TEST_OBJECTS) $(LIBS)

$(QUEUE_TEST_TARGET): $(QUEUE_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(QUEUE_TEST_TARGET) $(LDFLAGS) $(QUEUE_TEST_OBJECTS) $(LIBS)

$(THROUGHPUT_TEST_TARGET): $(THROUGHPUT_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(THROUGHPUT_TEST_TARGET) $(LDFLAGS) $(THROUGHPUT_TEST_OBJECTS) $(LIBS)

%.test.o: %.c $(GENERATED) Makefile
	@echo CC $@
	$(E)$(CC) $(CFLAGS) -c -o $@ $<

# FIXME: make dependency handling work for MinGW
ifneq ($(PLATFORM),Windows)
	-include $(subst .c,.test.d,$(SOURCES))
endif

%.test.d: %.c Makefile
	$(E)$(CC) -M $(CFLAGS) $< > $@.tmp; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.tmp > $@; \
	rm -f -- $@.tmp
