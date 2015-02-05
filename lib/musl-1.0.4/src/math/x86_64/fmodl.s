        .global fmodl
        .align 16, 0x90
        .type fmodl,@function
fmodl:
	fldt 24(%rsp)
	fldt 8(%rsp)
1:	fprem
	fstsw %ax
	sahf
	jp 1b
	fstp %st(1)
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_fmodl:
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
	.ascii	"{ fmodl\nY x86_fp80!x86_fp80@x86_fp80@\nR fmodl\n}"
	.byte	0
