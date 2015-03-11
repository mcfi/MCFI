# Makefile for the runtime
# Add -DNO_ONLINE_PATCHING for pure MCFI

CC = clang

CFLAGS = -O3 -g -fno-stack-protector -fno-strict-aliasing -I./include -nostdinc -fPIC

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

.PHONY: all $(RTIME) clean

all: $(RTIME)
	mkdir -p $(SDK)/bin
	cp  $(RTIME) $(SDK)/bin/

clean:
	rm -f $(OBJS)
	rm -f $(RTIME)

%.o: %.s
	$(CC) -c -o $@ $<

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RTIME): $(OBJS)
	$(LD) $(LDFLAGS) -o $(RTIME) $(OBJS)
