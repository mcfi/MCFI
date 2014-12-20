/* Copyright 2011-2012 Nicholas J. Kain, licensed under standard MIT license */
        .global sigsetjmp
        .align 16, 0x90
        .type sigsetjmp,@function
sigsetjmp:
	andl %esi,%esi
	movq %rsi,64(%edi)
	jz 1f
	pushq %rdi
	leaq 72(%rdi),%rdx
	xorl %esi,%esi
	movl $2,%edi
        .byte 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00
	call sigprocmask
__mcfi_dcj_sigprocmask:        
	popq %rdi
1:	jmp setjmp
