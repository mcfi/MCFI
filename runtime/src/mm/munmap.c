#include <mm.h>
#include <syscall.h>
#include <errno.h>

int munmap(void *start, size_t len)
{
  int rc = __syscall2(SYS_munmap, (long)start, len);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
