/* Copyright 2011-2012 Nicholas J. Kain, licensed under standard MIT license */
        .global __setjmp
        .global _setjmp
        .global setjmp
        .align 16, 0x90
        .type __setjmp,@function
        .type _setjmp,@function
        .type setjmp,@function
__setjmp:
_setjmp:
setjmp:
	mov %rbx,(%edi)         /* rdi is jmp_buf, move registers onto it */
	mov %rbp,8(%edi)
	mov %r12,16(%edi)
	mov %r13,24(%edi)
	mov %r14,32(%edi)
	mov %r15,40(%edi)
	lea 8(%rsp),%rdx        /* this is our rsp WITHOUT current ret addr */
	mov %rdx,48(%edi)
	mov (%rsp),%rdx         /* save return addr ptr for new rip */
	mov %rdx,56(%edi)
	xor %rax,%rax           /* always return 0 */
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_setjmp:     
        cmpq %rdi, %gs:(%rcx)
        jne check
        jmpq *%rcx
check:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        hlt

        .section	.MCFIFuncInfo,"",@progbits        
	.ascii	"{ setjmp\nR setjmp longjmp\n}"
	.byte	0
	.ascii	"{ _setjmp\nR setjmp longjmp\n}"
	.byte	0
	.ascii	"{ __setjmp\nR setjmp longjmp\n}"
	.byte	0
