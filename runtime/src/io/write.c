#include <io.h>
#include <syscall.h>

ssize_t write(int fd, const void *buf, size_t count)
{
  return __syscall3(SYS_write, fd, buf, count);
}
