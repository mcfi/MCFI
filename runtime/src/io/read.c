#include <io.h>
#include <syscall.h>

ssize_t read(int fd, void *buf, size_t count)
{
  return __syscall3(SYS_read, fd, buf, count);
}
