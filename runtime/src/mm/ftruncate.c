#include <mm.h>
#include <syscall.h>
#include <errno.h>

int ftruncate(int fd, off_t length)
{
  int rc = __syscall2(SYS_ftruncate, fd, __SYSCALL_LL_O(length));
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }  
  return rc;
}
