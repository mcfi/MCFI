        .global memset
        .align 16, 0x90
        .type memset,@function
memset:
        # save the return address to a scratch register
        popq %r11
        movl %edi, %edi
	and $0xff,%esi
	mov $0x101010101010101,%rax
	mov %rdx,%rcx
	mov %rdi,%r8
	imul %rsi,%rax
	cmp $16,%rcx
	jb 1f

	mov %rax,-8(%edi,%ecx)
	shr $3,%rcx
	rep
	stosq
	mov %r8,%rax
	jmp 2f

1:	test %ecx,%ecx
	jz 1f

	mov %al,(%edi)
	mov %al,-1(%edi,%ecx)
	cmp $2,%ecx
	jbe 1f

	mov %al,1(%edi)
	mov %al,-2(%edi,%ecx)
	cmp $4,%ecx
	jbe 1f

	mov %eax,(%edi)
	mov %eax,-4(%edi,%ecx)
	cmp $8,%ecx
	jbe 1f

	mov %eax,4(%edi)
	mov %eax,-8(%edi,%ecx)

1:	mov %r8,%rax
2:      #ret
        movl %r11d, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_memset:
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
        .ascii	"{ memset\nY i8*!i8*@i32@i64@\nR memset\n}"
	.byte	0
