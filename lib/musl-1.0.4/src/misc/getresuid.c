#define _GNU_SOURCE
#include <unistd.h>
#include "syscall.h"

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
  return syscall(SYS_getresuid,
                 mcfi_sandbox_mask(ruid), mcfi_sandbox_mask(euid),
                 mcfi_sandbox_mask(suid));
}
