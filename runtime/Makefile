# Makefile for the runtime

CC = gcc
AS = as

# during context switch, we do not to save/restore xmm registers, therefore,
# we shutdown all XMM register features such as SSE.
CFLAGS = -Os -g -march=native -mtune=native -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2 -fno-stack-protector -fno-strict-aliasing -I./include -nostdinc -fPIC

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
