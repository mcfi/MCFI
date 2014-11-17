#include <sys/sem.h>
#include "syscall.h"
#include "ipc.h"

int semop(int id, struct sembuf *buf, size_t n)
{
#ifdef SYS_semop
  return syscall(SYS_semop, id, mcfi_sandbox_mask(buf), n);
#else
  return syscall(SYS_ipc, IPCOP_semop, id, n, 0, mcfi_sandbox_mask(buf));
#endif
}
