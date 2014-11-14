#include <io.h>
#include <syscall.h>
#include <errno.h>

off_t lseek(int fd, off_t offset, int whence)
{
  off_t rc = (off_t)__syscall3(SYS_lseek, fd, offset, whence);
  if (rc < 0) {
    errn = -rc;
    rc = (off_t)-1;
  }
  return rc;
}
