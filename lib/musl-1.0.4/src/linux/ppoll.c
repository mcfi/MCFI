#define _GNU_SOURCE
#include <poll.h>
#include <signal.h>
#include "syscall.h"

int ppoll(struct pollfd *fds, nfds_t n, const struct timespec *to, const sigset_t *mask)
{
  return syscall_cp(SYS_ppoll, mcfi_sandbox_mask(fds), n,
                    to ? (struct timespec []){*to} : 0, mask, _NSIG/8);
}
