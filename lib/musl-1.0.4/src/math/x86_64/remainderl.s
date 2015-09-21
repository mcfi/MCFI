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
        movq %gs:(%rcx), %rsi
        cmpq %rdi, %rsi
        jne check
        # addq $1, %fs:0x108 # icj_count
go:
        jmpq *%rcx
check:
        cmpb  $0xfc, %sil
        je    go
        testb $0x1, %sil
        jz die
        cmpl %esi, %edi
        jne try
die:
        leaq try(%rip), %rdi
        jmp __report_cfi_violation_for_return@PLT

	.section	.MCFIFuncInfo,"",@progbits
	.ascii	"{ remainderl\nY x86_fp80!x86_fp80@x86_fp80@\nR remainderl\n}"
	.byte	0
