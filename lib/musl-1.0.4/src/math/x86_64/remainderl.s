        .global remainderl
        .align 16, 0x90
        .type remainderl,@function
remainderl:
	fldt 24(%rsp)
	fldt 8(%rsp)
1:	fprem1
	fstsw %ax
	sahf
	jp 1b
	fstp %st(1)
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_remainderl:
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
	.ascii	"{ remainderl\nY x86_fp80!x86_fp80@x86_fp80@\nR remainderl\n}"
	.byte	0
