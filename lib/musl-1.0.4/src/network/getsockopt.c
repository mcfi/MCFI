#include <sys/socket.h>
#include "syscall.h"

int getsockopt(int fd, int level, int optname, void *restrict optval, socklen_t *restrict optlen)
{
  return socketcall(getsockopt, fd, level, optname,
                    mcfi_sandbox_mask(optval), mcfi_sandbox_mask(optlen), 0);
}
