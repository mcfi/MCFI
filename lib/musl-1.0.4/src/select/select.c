#include <sys/select.h>
#include "syscall.h"
#include "libc.h"

int select(int n, fd_set *restrict rfds, fd_set *restrict wfds, fd_set *restrict efds, struct timeval *restrict tv)
{
	return syscall_cp(SYS_select, n,
                          mcfi_sandbox_mask(rfds),
                          mcfi_sandbox_mask(wfds),
                          mcfi_sandbox_mask(efds),
                          mcfi_sandbox_mask(tv));
}
