#include <sys/msg.h>
#include "syscall.h"
#include "ipc.h"

int msgctl(int q, int cmd, struct msqid_ds *buf)
{
#ifdef SYS_msgctl
  return syscall(SYS_msgctl, q, cmd | IPC_64, mcfi_sandbox_mask(buf));
#else
  return syscall(SYS_ipc, IPCOP_msgctl, q, cmd | IPC_64, 0, mcfi_sandbox_mask(buf), 0);
#endif
}
