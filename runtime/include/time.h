#ifndef TIME_H
#define TIME_H

#include <syscall.h>

struct timeval {
  time_t tv_sec;
  suseconds_t tv_usec;
};

static int gettimeofday(struct timeval *tv) {
  return __syscall2(SYS_gettimeofday, (long)tv, 0);
}

#endif
