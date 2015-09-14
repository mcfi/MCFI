        .global memmove
        .align 16, 0x90
        .type memmove,@function
memmove:
        movl %edi, %edi
	mov %rdi,%rax
	sub %rsi,%rax
	cmp %rdx,%rax
	jae memcpy
        # pop out the return address so the following
        # execution won't change it.
        popq %r11
	mov %rdx,%rcx
	leal -1(%rdi,%rdx),%edi
	lea -1(%rsi,%rdx),%rsi
	std
	rep movsb
	cld
	lea 1(%rdi),%rax
	#ret
        movl %r11d, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_memmove:
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
	.ascii	"{ memmove\nY i8*!i8*@i8*@i64@\nT memcpy\nR memmove\n}"
	.byte	0
