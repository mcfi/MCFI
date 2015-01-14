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
	.ascii	"{ fabs\nTY double!double@\nRT fabs\n}"
	.byte	0
