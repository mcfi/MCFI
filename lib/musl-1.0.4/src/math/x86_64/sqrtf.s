        .global sqrtf
        .align 16, 0x90
        .type sqrtf,@function
sqrtf:  sqrtss %xmm0, %xmm0
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_sqrtf:
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
	.ascii	"{ sqrtf\nY float!float@\nR sqrtf\n}"
	.byte	0
