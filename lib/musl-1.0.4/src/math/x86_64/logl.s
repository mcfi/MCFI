        .global logl
        .align 16, 0x90
        .type logl,@function
logl:
	fldln2
	fldt 8(%rsp)
	fyl2x
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_logl:
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
        .ascii	"{ logl\nY x86_fp80!x86_fp80@\nR logl\n}"
	.byte	0
