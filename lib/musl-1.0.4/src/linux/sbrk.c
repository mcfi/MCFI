#include <stdint.h>
#include <errno.h>
#include "syscall.h"

/* MUSL implements a safe sbrk ! :) */
void *sbrk(intptr_t inc)
{
	if (inc) return (void *)__syscall_ret(-ENOMEM);
	return (void *)__syscall(SYS_brk, 0);
}
