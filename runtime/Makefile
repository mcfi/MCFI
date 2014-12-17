# Makefile for the runtime

CC = clang

# during context switch, we do not to save/restore xmm registers, therefore,
# we shutdown all XMM register features such as SSE.
CFLAGS = -O3 -g -march=native -mtune=native -fno-stack-protector -fno-strict-aliasing -I./include -nostdinc -fPIC

SRCS = $(sort $(wildcard src/*.c src/*/*.c))
OBJS = $(SRCS:.c=.o)
OBJS += src/start.o
OBJS += src/runtime_interface.o
INCLUDES = $(sort $(wildcard include/*.h))

RTIME = rock

LD = /usr/bin/ld
LDFLAGS = -nostdlib -pie -e _start -Bsymbolic -Bsymbolic-functions

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
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RTIME): $(OBJS)
	$(LD) $(LDFLAGS) -o $(RTIME) $(OBJS)
