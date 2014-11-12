# Makefile for the runtime

CC = gcc
AS = as
CFLAGS = -O2 -ggdb -fno-strict-aliasing -I./include -nostdinc -fPIC

SRCS = $(sort $(wildcard src/*.c src/*/*.c))
OBJS = $(SRCS:.c=.o)
OBJS += src/start.o
RTIME = rock

LD = ld
LDFLAGS = -dynamic-linker rock -nostdlib -pie -e _start -Bsymbolic -Bsymbolic-functions

.PHONY: runtime clean

all: runtime

clean:
	rm -f $(OBJS)
	rm -f $(RTIME)

%.o: %.s
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

runtime: $(OBJS)
	$(LD) $(LDFLAGS) -o $(RTIME) $(OBJS)
