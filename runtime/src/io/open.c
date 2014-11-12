#include <io.h>
#include <syscall.h>

int open(const char *filename, int flags, mode_t mode)
{
  return __syscall3(SYS_open, filename, flags|O_LARGEFILE, mode);
}
