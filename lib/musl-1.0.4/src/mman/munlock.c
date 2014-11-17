#include <sys/mman.h>
#include "syscall.h"

int munlock(const void *addr, size_t len)
{
  return syscall(SYS_munlock, mcfi_sandbox_mask(addr), mcfi_sandbox_mask(len));
}
