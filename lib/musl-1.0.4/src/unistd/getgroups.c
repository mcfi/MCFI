#include <unistd.h>
#include "syscall.h"

int getgroups(int count, gid_t list[])
{
  return syscall(SYS_getgroups, count, mcfi_sandbox_mask(list));
}
