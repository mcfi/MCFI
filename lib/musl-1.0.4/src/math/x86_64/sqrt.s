        .global sqrt
        .align 16, 0x90
        .type sqrt,@function
sqrt:	sqrtsd %xmm0, %xmm0
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_sqrt:
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
	.ascii	"{ sqrt\nTY double!double@\nRT sqrt\n}"
	.byte	0
