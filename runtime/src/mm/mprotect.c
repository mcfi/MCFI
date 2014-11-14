#include <mm.h>
#include <syscall.h>
#include <errno.h>

int mprotect(void *addr, size_t len, int prot)
{
  size_t start, end;
  start = (size_t)addr & -PAGE_SIZE;
  end = (size_t)((char *)addr + len + PAGE_SIZE-1) & -PAGE_SIZE;
  int rc = __syscall3(SYS_mprotect, (long)start, end-start, prot);
  if (rc < 0) {
    errn = -rc;
    rc = -1;
  }
  return rc;
}
