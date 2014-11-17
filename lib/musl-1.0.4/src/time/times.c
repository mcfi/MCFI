#include <sys/times.h>
#include "syscall.h"

clock_t times(struct tms *tms)
{
  return __syscall(SYS_times, mcfi_sandbox_mask(tms));
}
