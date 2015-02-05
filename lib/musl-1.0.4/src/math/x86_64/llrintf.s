        .global llrintf
        .align 16, 0x90
        .type llrintf,@function
llrintf:
	cvtss2si %xmm0,%rax
	#ret
        popq %rcx
        movl %ecx, %ecx
try:    movq %gs:0x1000, %rdi
__mcfi_bary_llrintf:
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
	.ascii	"{ llrintf\nY i64!float@\nR llrintf\n}"
	.byte	0
