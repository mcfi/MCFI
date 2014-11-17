#include <sys/socket.h>
#include "syscall.h"

int getpeername(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
  return socketcall(getpeername, fd, mcfi_sandbox_mask(addr), mcfi_sandbox_mask(len), 0, 0, 0);
}
