#include <io.h>
#include <syscall.h>

off_t lseek(int fd, off_t offset, int whence)
{
  return __syscall3(SYS_lseek, fd, offset, whence);
}
