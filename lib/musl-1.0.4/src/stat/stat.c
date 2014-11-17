#include <sys/stat.h>
#include "syscall.h"
#include "libc.h"

int stat(const char *restrict path, struct stat *restrict buf)
{
  return syscall(SYS_stat, path, mcfi_sandbox_mask(buf));
}

LFS64(stat);
