#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "syscall.h"

int sigaltstack(const stack_t *restrict ss, stack_t *restrict old)
{
	if (ss) {
		if (ss->ss_size < MINSIGSTKSZ) {
			errno = ENOMEM;
			return -1;
		}
		if (ss->ss_flags & ~SS_DISABLE) {
			errno = EINVAL;
			return -1;
		}
                if ((size_t)ss->ss_sp > (size_t)0x40000000UL) {
                  fprintf(stderr, "sigaltstack registering stack outside of the sandbox\n");
                  exit(-1);
                }
	}
        if (old && ((size_t)old->ss_sp > (size_t)0x40000000UL)) {
          fprintf(stderr, "sigaltstack saving old stack outside of the sandbox\n");
          exit(-1);
        }
	return syscall(SYS_sigaltstack, mcfi_sandbox_mask(ss), mcfi_sandbox_mask(old));
}
