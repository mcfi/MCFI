#include <mm.h>
#include <syscall.h>

int munmap(void *start, size_t len)
{
  return __syscall2(SYS_munmap, start, len);
}
