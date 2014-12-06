#include <stdarg.h>
#include <unistd.h>
#include "pthread_impl.h"
#include "syscall.h"
#include "trampolines.h"
#include <sched.h>

int clone(int (*func)(void *), void *stack, int flags, void *arg, ...)
{
	va_list ap;
	pid_t *ptid, *ctid;
	void  *tls;
        void  *trusted_tls;
        
	va_start(ap, arg);
	ptid = va_arg(ap, pid_t *);
	tls  = va_arg(ap, void *);
	ctid = va_arg(ap, pid_t *);
	va_end(ap);
        trusted_tls = tls;
#ifdef _GNU_SOURCE
        if (flags & CLONE_SETTLS)
          trusted_tls = (void*) trampoline_allocset_tcb(tls);
#endif
	int rv = __syscall_ret(__clone(func, stack, flags, arg, ptid, trusted_tls, ctid));
#ifdef _GNU_SOURCE        
        if (rv < 0 && trusted_tls) {
          trampoline_free_tcb(tls);
        }
#endif        
        return rv;
}
