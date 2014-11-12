#include <mm.h>
#include <syscall.h>

int ftruncate(int fd, off_t length)
{
  return __syscall2(SYS_ftruncate, fd, __SYSCALL_LL_O(length));
}
