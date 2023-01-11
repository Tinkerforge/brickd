#
# For MinGW builds on Windows
#

## CONFIG #####################################################################

CC ?= gcc
WINDRES ?= windres

WITH_HOST ?= check
WITH_TARGET ?= check
WITH_DEBUG ?= no

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

ifeq ($(WITH_TARGET),check)
	override WITH_TARGET := $(WITH_HOST)
endif

ifndef libdir
	libdir := $(prefix)/lib
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	E := @
endif
endif

SOURCES := main.c
OBJECTS := ${SOURCES:.c=.o}
DEPENDS := ${SOURCES:.c=.p}

ifeq ($(WITH_TARGET),Windows)
	OBJECTS += logviewer.o
	TARGET := logviewer.exe
ifeq ($(WITH_HOST),Windows)
	DIST := ..\..\..\dist\logviewer.exe
else
	DIST := ../../../dist/logviewer.exe
endif
else
	TARGET := logviewer
	DIST :=
endif

ifeq ($(WITH_DEBUG),yes)
	override CFLAGS += -Og
else
ifeq ($(findstring -O,$(CFLAGS)),)
	override CFLAGS += -O2
endif
endif

ifeq ($(WITH_DEBUG),yes)
	override CFLAGS += -g -ggdb
endif

# tell GCC to use C99, but actually write C89 code plus stdint.h and stdbool.h
# to be able to compile it with MSVC which supports C89 only
override CFLAGS += -std=gnu99 -Wall -Wextra -Wno-stringop-truncation

ifeq ($(WITH_GPROF),yes)
	override CFLAGS += -pg -no-pie
	override LDFLAGS += -pg -no-pie
endif

ifeq ($(WITH_TARGET),Windows)
	override CFLAGS += -DWIN32_LEAN_AND_MEAN -DNDEBUG -DWINVER=0x0600 -D_WIN32_WINNT=0x0600 -mwindows
	override LDFLAGS += -Wl,-subsystem,windows
	override LIBS += -ladvapi32 -lcomctl32 -lcomdlg32 -luser32 -lshell32 -lole32
else
	override CFLAGS += -DSYSCONFDIR="\"$(sysconfdir)\"" -DLOCALSTATEDIR="\"$(localstatedir)\""
	override LDFLAGS += -pthread
endif

ifneq ($(MAKECMDGOALS),clean)
$(info platform:)
$(info - host:     $(WITH_HOST))
$(info - target:   $(WITH_TARGET))
$(info features:)
$(info - debug:    $(WITH_DEBUG))
$(info options:)
$(info - CFLAGS:   $(CFLAGS))
$(info - LDFLAGS:  $(LDFLAGS))
$(info - LIBS:     $(LIBS))
endif

.PHONY: all clean install

all: $(DIST) $(TARGET) Makefile

..\..\..\dist\logviewer.exe: $(TARGET) Makefile
	$(E)if not exist ..\..\..\dist\ mkdir ..\..\..\dist\
	@echo CP logviewer.exe
	$(E)copy "logviewer.exe" "..\..\..\dist\"

../../../dist/logviewer.exe: $(TARGET) Makefile
	$(E)mkdir -p ../../../dist/
	@echo CP logviewer.exe
	$(E)cp logviewer.exe ../../../dist/

clean: Makefile
	$(E)$(RM) $(OBJECTS) $(TARGET) $(DEPENDS)

$(TARGET): $(OBJECTS) Makefile
	@echo LD $@
	$(E)$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS) $(LIBS)

%.o: %.c Makefile
	@echo CC $@
	$(E)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.rc Makefile
	@echo GEN $@
	$(E)$(WINDRES) $< $@
