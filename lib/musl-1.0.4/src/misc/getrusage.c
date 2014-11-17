#include <sys/resource.h>
#include "syscall.h"

int getrusage(int who, struct rusage *ru)
{
  return syscall(SYS_getrusage, who, mcfi_sandbox_mask(ru));
}
