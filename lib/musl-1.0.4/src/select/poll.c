#include <poll.h>
#include "syscall.h"
#include "libc.h"

int poll(struct pollfd *fds, nfds_t n, int timeout)
{
  return syscall_cp(SYS_poll, mcfi_sandbox_mask(fds), n, timeout);
}
