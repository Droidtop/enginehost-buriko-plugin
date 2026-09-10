# ------------------------------------------------------------------------------
#
# Makefile for OpenBGI
#
# ------------------------------------------------------------------------------

# Name of the executable
TARGET      := openbgi

# Directories
SRCDIR      := src
BUILDDIR    := bin

# C sources
CSOURCES    := $(shell find $(SRCDIR) -name '*.c')

BUILD_NUMBER_FILE := $(SRCDIR)/build_number.h
BUMP_SCRIPT       := ./scripts/bump_build_number.sh

# Additional libraries
LIBRARIES   :=

INCDIR := src

# ------------------------------------------------------------------------------
#
# Tools
#
# ------------------------------------------------------------------------------

# Compile & Link, Compile, Assemble and Link utilities
CC = gcc #i686-w64-mingw32-gcc

# Compiler, assembler and linker options. OPT and SAN are overridable because the
# boot is composed frame by frame in real time: the whole screen is blended every
# frame until dirty rectangles exist, and at -O0 under the address sanitiser that
# is slow enough that the engine's own clock falls far behind the wall clock and a
# script that waits on time takes minutes to get anywhere. `make OPT=-O2 SAN=`
# builds the same engine fast enough to watch a boot through; the default is
# unchanged, sanitised and unoptimised, because that is what finds the bugs.
OPT ?=
SAN ?= -fsanitize=address
CFLAGS = -g $(OPT) $(SAN) `sdl2-config --cflags` -DSPNG_USE_MINIZ
LDFLAGS = -g $(SAN) `sdl2-config --libs` -lm

# System utilities
RM = rm -f

# ------------------------------------------------------------------------------
#
# Flags and argument finalization
#
# ------------------------------------------------------------------------------

# Finalizing flags
CFLAGS += $(patsubst %,-I%,$(INCDIR))

# Get object names
OBJS_ = $(CSOURCES:.c=.o)
OBJS = $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(OBJS_)) $(LIBRARIES)
PATHS = $(sort $(dir $(OBJS)))

# ------------------------------------------------------------------------------
#
# Targets
#
# ------------------------------------------------------------------------------

all: setup clean $(BUILDDIR)/$(TARGET)

setup:
	mkdir -p $(PATHS)

$(BUILDDIR)/$(TARGET): $(OBJS) $(BUILD_NUMBER_FILE)
	$(CC) $(OBJS) $(LDFLAGS) -o $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TOOLS):
	echo $@
	$(MAKE) -C $@

$(BUILD_NUMBER_FILE):
	@$(BUMP_SCRIPT) $(BUILD_NUMBER_FILE)

clean:
	$(RM) $(OBJS) $(BUILDDIR)/$(TARGET)

.PHONY: all setup $(TOOLS) $(BUILD_NUMBER_FILE)

# EOF --------------------------------------------------------------------------