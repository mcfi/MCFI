        .global memset
        .align 16, 0x90
        .type memset,@function
memset:
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
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_memset:
        cmpq %rdi, %gs:(%rcx)
        jne check
        jmpq *%rcx
check:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die
        cmpl %esi, %edi
        jne try
die:
        hlt
       
        .section	.MCFIFuncInfo,"",@progbits
        .ascii	"{ memset\nTY i8*!i8*@i32@i64@\nRT memset\n}"
	.byte	0
