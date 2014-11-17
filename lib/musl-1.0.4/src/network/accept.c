#include <sys/socket.h>
#include "syscall.h"
#include "libc.h"

int accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
  return socketcall_cp(accept, fd, mcfi_sandbox_mask(addr), mcfi_sandbox_mask(len), 0, 0, 0);
}
