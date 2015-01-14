        .global feclearexcept
        .align 16, 0x90
        .type feclearexcept,@function
feclearexcept:
		# maintain exceptions in the sse mxcsr, clear x87 exceptions
	mov %edi,%ecx
	and $0x3f,%ecx
	fnstsw %ax
	test %eax,%ecx
	jz 1f
	fnclex
1:	stmxcsr -8(%rsp)
	and $0x3f,%eax
	or %eax,-8(%rsp)
	test %ecx,-8(%rsp)
	jz 1f
	not %ecx
	and %ecx,-8(%rsp)
	ldmxcsr -8(%rsp)
1:	xor %eax,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try1:   movq %gs:0x1000, %rdi
__mcfi_bary_feclearexcept:
        cmpq %rdi, %gs:(%rcx)
        jne check1
        jmpq *%rcx
check1:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die1
        cmpl %esi, %edi
        jne try1
die1:
        hlt

        .global feraiseexcept
        .align 16, 0x90
        .type feraiseexcept,@function
feraiseexcept:
	and $0x3f,%edi
	stmxcsr -8(%rsp)
	or %edi,-8(%rsp)
	ldmxcsr -8(%rsp)
	xor %eax,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try2:   movq %gs:0x1000, %rdi
__mcfi_bary_feraiseexcept:
        cmpq %rdi, %gs:(%rcx)
        jne check2
        jmpq *%rcx
check2:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die2
        cmpl %esi, %edi
        jne try2
die2:
        hlt


        .global __fesetround
        .align 16, 0x90
        .type __fesetround,@function
__fesetround:
	push %rax
	xor %eax,%eax
	mov %edi,%ecx
	fnstcw (%rsp)
	andb $0xf3,1(%rsp)
	or %ch,1(%rsp)
	fldcw (%rsp)
	stmxcsr (%rsp)
	shl $3,%ch
	andb $0x9f,1(%rsp)
	or %ch,1(%rsp)
	ldmxcsr (%rsp)
	pop %rcx
	#ret
        popq %rcx
        movl %ecx, %ecx
try3:   movq %gs:0x1000, %rdi
__mcfi_bary___fesetround:
        cmpq %rdi, %gs:(%rcx)
        jne check3
        jmpq *%rcx
check3:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die3
        cmpl %esi, %edi
        jne try3
die3:
        hlt

        .global fegetround
        .align 16, 0x90
        .type fegetround,@function
fegetround:
	push %rax
	stmxcsr (%rsp)
	pop %rax
	shr $3,%eax
	and $0xc00,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try4:   movq %gs:0x1000, %rdi
__mcfi_bary_fegetround:
        cmpq %rdi, %gs:(%rcx)
        jne check4
        jmpq *%rcx
check4:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die4
        cmpl %esi, %edi
        jne try4
die4:
        hlt

        .global fegetenv
        .align 16, 0x90
        .type fegetenv,@function
fegetenv:
	xor %eax,%eax
	fnstenv (%edi)
	stmxcsr 28(%edi)
	#ret
        popq %rcx
        movl %ecx, %ecx
try5:   movq %gs:0x1000, %rdi
__mcfi_bary_fegetenv:
        cmpq %rdi, %gs:(%rcx)
        jne check5
        jmpq *%rcx
check5:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die5
        cmpl %esi, %edi
        jne try5
die5:
        hlt

        .global fesetenv
        .align 16, 0x90
        .type fesetenv,@function
fesetenv:
	xor %eax,%eax
	inc %rdi
	jz 1f
	fldenv -1(%rdi)
	ldmxcsr 27(%rdi)
	jmp 2f
1:	push %rax
	push %rax
	pushq $0xffff
	pushq $0x37f
	fldenv (%rsp)
	pushq $0x1f80
	ldmxcsr (%rsp)
	add $40,%esp
	#ret
2:
        popq %rcx
        movl %ecx, %ecx
try6:   movq %gs:0x1000, %rdi
__mcfi_bary_fesetenv:
        cmpq %rdi, %gs:(%rcx)
        jne check6
        jmpq *%rcx
check6:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die6
        cmpl %esi, %edi
        jne try6
die6:
        hlt

        .global fetestexcept
        .align 16, 0x90
        .type fetestexcept,@function
fetestexcept:
	and $0x3f,%edi
	push %rax
	stmxcsr (%rsp)
	pop %rsi
	fnstsw %ax
	or %esi,%eax
	and %edi,%eax
	#ret
        popq %rcx
        movl %ecx, %ecx
try7:   movq %gs:0x1000, %rdi
__mcfi_bary_fetestexcept:
        cmpq %rdi, %gs:(%rcx)
        jne check7
        jmpq *%rcx
check7:
        movq %gs:(%rcx), %rsi
        testb $0x1, %sil
        jne die7
        cmpl %esi, %edi
        jne try7
die7:
        hlt

        .section	.MCFIFuncInfo,"",@progbits
        .ascii	"{ feclearexcept\nTY i32!i32@\nRT feclearexcept\n}"
	.byte	0
        .ascii	"{ feraiseexcept\nTY i32!i32@\nRT feraiseexcept\n}"
	.byte	0
        .ascii	"{ fegetround\nTY i32!\nRT fegetround\n}"
	.byte	0
	.ascii	"{ fegetenv\nTY i32!%struct.fenv_t*@\nRT fegetenv\n}"
	.byte	0
	.ascii	"{ fesetenv\nTY i32!%struct.fenv_t*@\nRT fesetenv\n}"
	.byte	0
	.ascii	"{ fetestexcept\nTY i32!i32@\nRT fetestexcept\n}"
	.byte	0
	.ascii	"{ __fesetround\nRT __fesetround\n}"
	.byte	0
