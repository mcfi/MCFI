#include <stdlib.h>
#include "syscall.h"
#include "trampolines.h"

_Noreturn void _Exit(int ec)
{
        trampoline_collect_stat();
	__syscall(SYS_exit_group, ec);
	for (;;) __syscall(SYS_exit, ec);
}
