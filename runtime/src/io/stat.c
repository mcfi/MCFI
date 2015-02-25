#include <io.h>
#include <syscall.h>
#include <errno.h>

int stat(const char * restrict path, struct stat * restrict buf) {
  int rc = (int)__syscall2(SYS_stat, (long)path, (long)buf);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}

int fstat(int fd, struct stat * buf) {
  int rc = (int)__syscall2(SYS_fstat, fd, (long)buf);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
