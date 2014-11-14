#include <io.h>
#include <syscall.h>
#include <errno.h>

int open(const char *filename, int flags, mode_t mode)
{
  int rc = (int)__syscall3(SYS_open, (long)filename, flags|O_LARGEFILE, mode);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
