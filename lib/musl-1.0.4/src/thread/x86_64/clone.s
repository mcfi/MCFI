        .text
        .global __clone
        .align 16, 0x90
        .type   __clone,@function
__clone:
	xor %eax,%eax
	mov $56,%al
	mov %rdi,%r11
	mov %rdx,%rdi
	mov %r8,%rdx
	mov %r9,%r8
	mov 8(%rsp),%r10
	mov %r11,%r9
	and $-16,%rsi
	sub $8,%rsi
	mov %rcx,(%esi)
	syscall
	test %eax,%eax
	jnz 1f
	xor %ebp,%ebp
	pop %rdi
        movl %r9d, %r9d
try1:   movq %gs:0x1000, %r11
__mcfi_bary___thread_start:
        cmpq %r11, %gs:(%r9)
        jne check1
        nop
        .byte 0x0f, 0x1f, 0x44, 0x00, 0x00 # 5-byte nop
	call *%r9
__mcfi_icj_1___thread_start:
	mov %eax,%edi
	xor %eax,%eax
	mov $60,%al
	syscall
	hlt
1:	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary___clone:
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
        leaq try(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

check1:
        movq %gs:(%r9), %r10
        testb $0x1, %r10b
        jz die1
        cmpl %r11d, %r10d
        jne try1
die1:
        leaq try1(%rip), %rdi
        movq %r9, %rsi
        jmp __report_cfi_violation@PLT

        .section	.MCFIFuncInfo,"",@progbits
        .ascii "{ __clone\nR __clone\n}"
        .byte 0

        .section	.MCFIIndirectCalls,"",@progbits
        .ascii "__thread_start#N#ThreadEntry"
        .byte 0
