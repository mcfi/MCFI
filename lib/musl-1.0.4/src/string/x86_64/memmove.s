        .global memmove
        .align 16, 0x90
        .type memmove,@function
memmove:
        movl %edi, %edi
	mov %rdi,%rax
	sub %rsi,%rax
	cmp %rdx,%rax
	jae memcpy
	mov %rdx,%rcx
	leal -1(%rdi,%rdx),%edi
	lea -1(%rsi,%rdx),%rsi
	std
	rep movsb
	cld
	lea 1(%rdi),%rax
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_memmove:
        cmpq %rdi, %gs:(%rcx)
        jne check
        # addq $1, %fs:0x108 # icj_count
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

        .section	.MCFIFuncInfo,"",@progbits
	.ascii	"{ memmove\nY i8*!i8*@i8*@i64@\nT memcpy\nR memmove\n}"
	.byte	0
