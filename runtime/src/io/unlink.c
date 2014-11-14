#include <io.h>
#include <syscall.h>
#include <errno.h>

int unlink(const char *path)
{
  int rc = __syscall1(SYS_unlink, (long)path);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
