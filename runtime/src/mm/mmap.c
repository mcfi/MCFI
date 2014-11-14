#include <mm.h>
#include <syscall.h>
#include <errno.h>

void *mmap(void *start, size_t len, int prot, int flags, int fd, off_t off)
{
  long rs = __syscall6(SYS_mmap, (long)start, len, prot, flags, fd, off);
  if (rs < 0) {
    errn = (unsigned) -rs;
    return MAP_FAILED;
  }
  return (void*)rs;
}
