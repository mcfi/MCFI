        .global fabsl
        .align 16, 0x90
        .type fabsl,@function
fabsl:
	fldt 8(%rsp)
	fabs
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_fabsl:
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
	.ascii	"{ fabsl\nTY x86_fp80!x86_fp80@\nRT fabsl\n}"
	.byte	0
