	.text
	.globl	strcat
	.align	16, 0x90
	.type	strcat,@function
strcat:                                 # @strcat
        # save the return address to a scratch register
        popq    %r11
# BB#0:                                 # %entry
	leaq	-1(%rdi), %rax
	.align	16, 0x90
.LBB0_1:                                # %while.cond
                                        # =>This Inner Loop Header: Depth=1
	cmpb	$0, 1(%rax)
	leaq	1(%rax), %rax
	jne	.LBB0_1
# BB#2:                                 # %while.cond1.preheader
	movb	(%rsi), %cl
	.byte	103
	movb	%cl, (%rax)
	testb	%cl, %cl
	je	.LBB0_5
# BB#3:                                 # %while.body3.preheader
	incq	%rax
	incq	%rsi
	.align	16, 0x90
.LBB0_4:                                # %while.body3
                                        # =>This Inner Loop Header: Depth=1
	movb	(%rsi), %cl
	.byte	103
	movb	%cl, (%rax)
	incq	%rax
	incq	%rsi
	testb	%cl, %cl
	jne	.LBB0_4
.LBB0_5:                                # %while.end6
	movq	%rdi, %rax
	movl	%r11d, %ecx
.LBB0_6:                                # =>This Inner Loop Header: Depth=1
	movq	%gs:4096, %rdi
	.hidden	__mcfi_bary_a6315766ed77499f
__mcfi_bary_a6315766ed77499f:
        movq	%gs:(%rcx), %rsi
	cmpq	%rdi, %rsi
	jne	.LBB0_8
# BB#7:
go:
	jmpq	*%rcx
.LBB0_8:                                #   in Loop: Header=BB0_6 Depth=1
        cmpb    $0xfc, %sil
        je      go
	testb	$1, %sil
	je	.LBB0_10
# BB#9:                                 #   in Loop: Header=BB0_6 Depth=1
	cmpl	%esi, %edi
	jne	.LBB0_6
.LBB0_10:
	leaq	.LBB0_6(%rip), %rdi
	jmp	__report_cfi_violation_for_return@PLT # TAILCALL
.Ltmp0:
	.size	strcat, .Ltmp0-strcat


	.section	.MCFIFuncInfo,"",@progbits
	.ascii	"{ strcat\nY i8*!i8*@i8*@\nR a6315766ed77499f\n}"
	.byte	0
