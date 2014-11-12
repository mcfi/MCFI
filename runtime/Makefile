# Makefile for the runtime

CC = gcc
AS = as
CFLAGS = -O0 -ggdb -fno-strict-aliasing -I./include -nostdinc -fPIC

SRCS = $(sort $(wildcard src/*.c src/*/*.c))
OBJS = $(SRCS:.c=.o)
OBJS += src/start.o
RTIME = rock

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
	$(CC) $(CFLAGS) -nostdlib -pie -Wl,-e,_start \
	-Wl,-Bsymbolic -Wl,-Bsymbolic-functions -o $(RTIME) $(OBJS)
