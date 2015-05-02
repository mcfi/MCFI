        .global fabs
        .align 16, 0x90
        .type fabs,@function
fabs:
	xor %eax,%eax
	dec %rax
	shr %rax
	movq %rax,%xmm1
	andpd %xmm1,%xmm0
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_fabs:
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
	.ascii	"{ fabs\nY double!double@\nR fabs\n}"
	.byte	0
