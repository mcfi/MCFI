#include <unistd.h>
#include <sys/time.h>
#include "syscall.h"

unsigned alarm(unsigned seconds)
{
	struct itimerval it = { .it_value.tv_sec = seconds };
	__syscall(SYS_setitimer, ITIMER_REAL, &it, mcfi_sandbox_mask(&it));
	return it.it_value.tv_sec + !!it.it_value.tv_usec;
}
