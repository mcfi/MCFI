/* Written 2011 Nicholas J. Kain, released as Public Domain */
.weak _init
.weak _fini
.text
.global _start
_start:
	xorl %ebp,%ebp    /* rbp:undefined -> mark as zero 0 (ABI) */
	movq %rdx,%r9     /* 6th arg: ptr to register with atexit() */
	popq %rsi         /* 2nd arg: argc */
	movl %esp,%edx   /* 3rd arg: argv */
	andl $-16,%esp   /* align stack pointer */
	leal _fini(%rip),%r8d           /* 5th arg: fini/dtors function */
	leal _init(%rip),%ecx          /* 4th arg: init/ctors function */
	movl main@GOTPCREL(%rip),%edi  /* 1st arg: application entry ip */
	callq __libc_start_main@PLT    /* musl init will run the program */
1:	jmp 1b
