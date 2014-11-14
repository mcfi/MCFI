#include <io.h>
#include <errno.h>
#include <syscall.h>

int close(int fd)
{
  int rc = (int)__syscall1(SYS_close, fd);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
