#include <sys/socket.h>
#include "syscall.h"
#include "libc.h"

ssize_t recvfrom(int fd, void *restrict buf, size_t len, int flags, struct sockaddr *restrict addr, socklen_t *restrict alen)
{
  return socketcall_cp(recvfrom, fd,
                       mcfi_sandbox_mask(buf), len, flags,
                       mcfi_sandbox_mask(addr), mcfi_sandbox_mask(alen));
}
