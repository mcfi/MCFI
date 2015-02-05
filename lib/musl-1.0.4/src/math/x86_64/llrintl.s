        .global llrintl
        .align 16, 0x90
        .type llrintl,@function
llrintl:
	fldt 8(%rsp)
	fistpll 8(%rsp)
	mov 8(%rsp),%rax
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_llrintl:
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
	.ascii	"{ llrintl\nY i64!x86_fp80@\nR llrintl\n}"
	.byte	0
