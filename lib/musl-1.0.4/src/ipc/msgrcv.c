#include <sys/msg.h>
#include "syscall.h"
#include "ipc.h"
#include "libc.h"

ssize_t msgrcv(int q, void *m, size_t len, long type, int flag)
{
#ifdef SYS_msgrcv
  return syscall_cp(SYS_msgrcv, q, mcfi_sandbox_mask(m), len, type, flag);
#else
  return syscall_cp(SYS_ipc, IPCOP_msgrcv, q, len, flag, ((long[]){ mcfi_sandbox_mask(m), type }));
#endif
}
