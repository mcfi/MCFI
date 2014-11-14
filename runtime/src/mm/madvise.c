#include <mm.h>
#include <syscall.h>
#include <errno.h>

int madvise(void *addr, size_t len, int advice)
{
  int rc = __syscall3(SYS_madvise, (long)addr, len, advice);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
