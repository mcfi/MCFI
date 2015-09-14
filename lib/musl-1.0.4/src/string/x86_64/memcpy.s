        .global memcpy
        .align 16, 0x90
        .type memcpy,@function
memcpy:
        # save the return address to a secure place so
        # that memcpy itself cannot change it
        popq %r11
        movl %edi, %edi
	mov %rdi,%rax
	cmp $8,%rdx
	jc 1f
	test $7,%edi
	jz 1f
2:	movsb
	dec %rdx
	test $7,%edi
	jnz 2b
1:	mov %rdx,%rcx
	shr $3,%rcx
	rep
	movsq
	and $7,%edx
	jz 1f
2:	movsb
	dec %edx
	jnz 2b
1:
        movl %r11d, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_memcpy:
        movq %gs:(%rcx), %rsi
        cmpq %rdi, %rsi
        jne check
        # addq $1, %fs:0x108 # icj_count
        jmpq *%rcx
check:
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        leaq try(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

        .section	.MCFIFuncInfo,"",@progbits
        .ascii "{ memcpy\nY i8*!i8*@i8*@i64@\nR memcpy\n}"
        .byte 0
