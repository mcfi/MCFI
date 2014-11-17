#include "syscall.h"

int capset(void *a, void *b)
{
  return syscall(SYS_capset, mcfi_sandbox_mask(a), mcfi_sandbox_mask(b));
}

int capget(void *a, void *b)
{
  return syscall(SYS_capget, mcfi_sandbox_mask(a), mcfi_sandbox_mask(b));
}
