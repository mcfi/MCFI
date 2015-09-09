# Makefile for the runtime
# add STAT=1 at command line to enable execution statistics data collection
# add MCFI=1 at command line to disable online code patching

CC = $(shell eval echo ${LLVM_HOME})/bin/clang

STAT ?= 0
MCFI ?= 0
NOCFI ?= 0
VERBOSE ?= 0
PROFILING ?= 0

CFLAGS = -fno-stack-protector -fno-strict-aliasing -I./include -nostdinc -fPIC
SRCS = $(sort $(wildcard src/*.c src/*/*.c))
OBJS = $(SRCS:.c=.o)
OBJS += src/start.o
OBJS += src/runtime_interface.o
INCLUDES = $(sort $(wildcard include/*.h include/*/*.h))

RTIME = rock

LD = /usr/bin/ld
LDFLAGS = -nostdlib -pie -e _start -Bsymbolic -Bsymbolic-functions -dynamic-linker=

HOMEDIR = $(shell eval echo ~)
SDK = $(shell eval echo ${MCFI_SDK})

ifeq ($(SDK), )
  SDK = $(HOMEDIR)/MCFI/toolchain
endif

.PHONY: all release debug staticcheck build clean

all: release
	@echo "Runtime successfully built and installed."

release: CFLAGS+=-O3
release: build

debug: CFLAGS+=-O0 -g
debug: build

staticcheck: CFLAGS+=--analyze
staticcheck: build

build:
ifeq ($(STAT), 1)
CFLAGS+=-DCOLLECT_STAT
endif
ifeq ($(MCFI), 1)
CFLAGS+=-DNO_ONLINE_PATCHING
endif
ifeq ($(NOCFI), 1)
CFLAGS+=-DNOCFI -DNO_ONLINE_PATCHING
endif
ifeq ($(NOJCV), 1)
CFLAGS+=-DNO_JITCODE_VERIFICATION
endif
ifeq ($(VERBOSE), 1)
CFLAGS+=-DVERBOSE
endif
ifeq ($(PROFILING), 1)
CFLAGS+=-DPROFILING
endif

build: $(RTIME)
	mkdir -p $(SDK)/bin
	cp  $(RTIME) $(SDK)/bin/

clean:
	rm -f $(OBJS)
	rm -f $(RTIME)

%.o: %.s
	$(CC) -c -o $@ $<

%.o: %.S
	$(CC) -c -o $@ $<

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RTIME): $(OBJS)
	$(LD) $(LDFLAGS) -o $(RTIME) $(OBJS)
