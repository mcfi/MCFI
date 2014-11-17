#include "futex.h"
#include "syscall.h"

int __futex(volatile int *addr, int op, int val, void *ts)
{
  return syscall(SYS_futex, mcfi_sandbox_mask(addr), op, val, mcfi_sandbox_mask(ts));
}
