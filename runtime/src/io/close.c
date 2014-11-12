#include <io.h>
#include <errno.h>
#include <syscall.h>

int close(int fd)
{
  return __syscall1(SYS_close, fd);
}
