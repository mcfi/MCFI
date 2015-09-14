/* Copyright 2011-2012 Nicholas J. Kain, licensed under standard MIT license */
        .global _longjmp
        .global longjmp
        .align 16, 0x90
        .type _longjmp,@function
        .type longjmp,@function
_longjmp:
longjmp:
	mov %rsi,%rax           /* val will be longjmp return */
	test %rax,%rax
	jnz 1f
	inc %rax                /* if val==0, val=1 per longjmp semantics */
1:
	mov (%rdi),%rbx         /* rdi is the jmp_buf, restore regs from it */
	mov 8(%rdi),%rbp
	mov 16(%rdi),%r12
	mov 24(%rdi),%r13
	mov 32(%rdi),%r14
	mov 40(%rdi),%r15
	mov 48(%rdi),%rdx       /* this ends up being the stack pointer */
	mov %edx,%esp
	mov 56(%rdi),%edx       /* this is the instruction pointer */
try:
5:
        mov %gs:0x1000, %rdi
__mcfi_bary_longjmp:
3:
        mov %gs:(%rdx), %rsi
        cmp %rdi, %rsi
        jne 2f
        # addq $1, %fs:0x108 # icj_count
	jmp *%rdx               /* goto saved address without altering rsp */
2:
        test $0x1, %sil
        jz 4f
        cmp %esi, %edi
        jne 5b
4:
        leaq try(%rip), %rdi
        movq %rdx, %rsi
        jmp __report_cfi_violation@PLT
