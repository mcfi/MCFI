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

static  struct timeval tv1, tv2;

#ifdef PROFILING
#define start_timer(str) gettimeofday(&tv1);

#define stop_timer(str) gettimeofday(&tv2);                             \
  dprintf(STDERR_FILENO, "%s (us): %u\n", str,                          \
          (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec));

#else
#define start_timer(str)
#define stop_timer(str)
#endif

#endif
