#include <io.h>
#include <syscall.h>

int unlink(const char *path)
{
  return __syscall1(SYS_unlink, path);
}
