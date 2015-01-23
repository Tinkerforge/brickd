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

ARRAY_TEST_SOURCES := array_test.c $(call FIX_PATH,../daemonlib/array.c)
QUEUE_TEST_SOURCES := queue_test.c $(call FIX_PATH,../daemonlib/queue.c)
THROUGHPUT_TEST_SOURCES := throughput_test.c ip_connection.c brick_master.c $(call FIX_PATH,../daemonlib/base58.c) $(call FIX_PATH,../daemonlib/utils.c)
SHA1_TEST_SOURCES := sha1_test.c $(call FIX_PATH,../brickd/sha1.c)
PUTENV_TEST_SOURCES := putenv_test.c
BASE58_TEST_SOURCES := base58_test.c $(call FIX_PATH,../daemonlib/base58.c) $(call FIX_PATH,../daemonlib/utils.c)
NODE_TEST_SOURCES := node_test.c $(call FIX_PATH,../daemonlib/node.c)
CONF_FILE_TEST_SOURCES := conf_file_test.c $(call FIX_PATH,../daemonlib/conf_file.c) $(call FIX_PATH,../daemonlib/array.c) $(call FIX_PATH,../daemonlib/base58.c) $(call FIX_PATH,../daemonlib/utils.c)

SOURCES := $(ARRAY_TEST_SOURCES) \
           $(QUEUE_TEST_SOURCES) \
           $(THROUGHPUT_TEST_SOURCES) \
           $(SHA1_TEST_SOURCES) \
           $(PUTENV_TEST_SOURCES) \
           $(BASE58_TEST_SOURCES) \
           $(NODE_TEST_SOURCES) \
           $(CONF_FILE_TEST_SOURCES)

ifeq ($(PLATFORM),Windows)
	ARRAY_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	QUEUE_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	THROUGHPUT_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	SHA1_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	PUTENV_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	BASE58_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	NODE_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
	CONF_FILE_TEST_SOURCES += $(call FIX_PATH,../brickd/fixes_mingw.c)
endif

ARRAY_TEST_OBJECTS := ${ARRAY_TEST_SOURCES:.c=.o}
QUEUE_TEST_OBJECTS := ${QUEUE_TEST_SOURCES:.c=.o}
THROUGHPUT_TEST_OBJECTS := ${THROUGHPUT_TEST_SOURCES:.c=.o}
SHA1_TEST_OBJECTS := ${SHA1_TEST_SOURCES:.c=.o}
PUTENV_TEST_OBJECTS := ${PUTENV_TEST_SOURCES:.c=.o}
BASE58_TEST_OBJECTS := ${BASE58_TEST_SOURCES:.c=.o}
NODE_TEST_OBJECTS := ${NODE_TEST_SOURCES:.c=.o}
CONF_FILE_TEST_OBJECTS := ${CONF_FILE_TEST_SOURCES:.c=.o}

OBJECTS := $(ARRAY_TEST_OBJECTS) \
           $(QUEUE_TEST_OBJECTS) \
           $(THROUGHPUT_TEST_OBJECTS) \
           $(SHA1_TEST_OBJECTS) \
           $(PUTENV_TEST_OBJECTS) \
           $(BASE58_TEST_OBJECTS) \
           $(NODE_TEST_OBJECTS) \
           $(CONF_FILE_TEST_OBJECTS)

DEPENDS := ${ARRAY_TEST_SOURCES:.c=.p} \
           ${QUEUE_TEST_SOURCES:.c=.p} \
           ${THROUGHPUT_TEST_SOURCES:.c=.p} \
           ${SHA1_TEST_SOURCES:.c=.p} \
           ${PUTENV_TEST_SOURCES:.c=.p} \
           ${BASE58_TEST_SOURCES:.c=.p} \
           ${NODE_TEST_SOURCES:.c=.p} \
           ${CONF_FILE_TEST_SOURCES:.c=.p}

ifeq ($(PLATFORM),Windows)
	ARRAY_TEST_TARGET := array_test.exe
	QUEUE_TEST_TARGET := queue_test.exe
	THROUGHPUT_TEST_TARGET := throughput_test.exe
	SHA1_TEST_TARGET := sha1_test.exe
	PUTENV_TEST_TARGET := putenv_test.exe
	BASE58_TEST_TARGET := base58_test.exe
	NODE_TEST_TARGET := node_test.exe
	CONF_FILE_TEST_TARGET := conf_file_test.exe
else
	ARRAY_TEST_TARGET := array_test
	QUEUE_TEST_TARGET := queue_test
	THROUGHPUT_TEST_TARGET := throughput_test
	SHA1_TEST_TARGET := sha1_test
	PUTENV_TEST_TARGET := putenv_test
	BASE58_TEST_TARGET := base58_test
	NODE_TEST_TARGET := node_test
	CONF_FILE_TEST_TARGET := conf_file_test
endif

TARGETS := $(ARRAY_TEST_TARGET) \
           $(QUEUE_TEST_TARGET) \
           $(THROUGHPUT_TEST_TARGET) \
           $(SHA1_TEST_TARGET) \
           $(PUTENV_TEST_TARGET) \
           $(BASE58_TEST_TARGET) \
           $(NODE_TEST_TARGET) \
           $(CONF_FILE_TEST_TARGET)

CFLAGS += -O2 -Wall -Wextra -I..
#CFLAGS += -O0 -g -ggdb

ifeq ($(PLATFORM),Windows)
	CFLAGS += -DWINVER=0x0500 -D_WIN32_WINNT=0x0501 -mconsole -include ../brickd/fixes_mingw.h
	LDFLAGS += -Wl,-subsystem,console
	LIBS += -lws2_32 -ladvapi32
else
	LDFLAGS += -pthread
endif

.PHONY: all clean

all: $(TARGETS) Makefile

clean: Makefile
	$(E)$(RM) $(GENERATED) $(OBJECTS) $(TARGETS) $(DEPENDS)

$(ARRAY_TEST_TARGET): $(ARRAY_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(ARRAY_TEST_TARGET) $(LDFLAGS) $(ARRAY_TEST_OBJECTS) $(LIBS)

$(QUEUE_TEST_TARGET): $(QUEUE_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(QUEUE_TEST_TARGET) $(LDFLAGS) $(QUEUE_TEST_OBJECTS) $(LIBS)

$(THROUGHPUT_TEST_TARGET): $(THROUGHPUT_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(THROUGHPUT_TEST_TARGET) $(LDFLAGS) $(THROUGHPUT_TEST_OBJECTS) $(LIBS)

$(SHA1_TEST_TARGET): $(SHA1_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(SHA1_TEST_TARGET) $(LDFLAGS) $(SHA1_TEST_OBJECTS) $(LIBS)

$(PUTENV_TEST_TARGET): $(PUTENV_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(PUTENV_TEST_TARGET) $(LDFLAGS) $(PUTENV_TEST_OBJECTS) $(LIBS)

$(BASE58_TEST_TARGET): $(BASE58_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(BASE58_TEST_TARGET) $(LDFLAGS) $(BASE58_TEST_OBJECTS) $(LIBS)

$(NODE_TEST_TARGET): $(NODE_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(NODE_TEST_TARGET) $(LDFLAGS) $(NODE_TEST_OBJECTS) $(LIBS)

$(CONF_FILE_TEST_TARGET): $(CONF_FILE_TEST_OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(CONF_FILE_TEST_TARGET) $(LDFLAGS) $(CONF_FILE_TEST_OBJECTS) $(LIBS)

%.o: %.c $(GENERATED) Makefile
	@echo CC $@
ifneq ($(PLATFORM),Windows)
	$(E)$(CC) -M $(CFLAGS) -o $*.d $<; cp $*.d $*.p; sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.p; rm -f $*.d
endif
	$(E)$(CC) $(CFLAGS) -c -o $@ $<

# FIXME: make dependency handling work for MinGW
# http://make.paulandlesley.org/autodep.html
ifneq ($(PLATFORM),Windows)
-include $(DEPENDS)
endif
