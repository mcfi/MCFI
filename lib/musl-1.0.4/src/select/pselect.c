#include <sys/select.h>
#include <signal.h>
#include "syscall.h"
#include "libc.h"

int pselect(int n, fd_set *restrict rfds, fd_set *restrict wfds, fd_set *restrict efds, const struct timespec *restrict ts, const sigset_t *restrict mask)
{
	long data[2] = { (long)mask, _NSIG/8 };
	struct timespec ts_tmp;
	if (ts) ts_tmp = *ts;
	return syscall_cp(SYS_pselect6, n,
                          mcfi_sandbox_mask(rfds), mcfi_sandbox_mask(wfds),
                          mcfi_sandbox_mask(efds), ts ? mcfi_sandbox_mask(&ts_tmp) : 0,
                          mcfi_sandbox_mask(data));
}
