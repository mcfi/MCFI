#include <mm.h>
#include <syscall.h>

int madvise(void *addr, size_t len, int advice)
{
  return __syscall3(SYS_madvise, addr, len, advice);
}
