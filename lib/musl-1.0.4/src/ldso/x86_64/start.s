.text
.global _start
        .align 16, 0x90
_start:
	mov (%rsp),%rdi
	lea 8(%rsp),%rsi
        nop
        nop
	call __dynlink
__mcfi_dcj___dynlink:
	pop %rdi
1:	dec %edi
	pop %rsi
	cmp $-1,%rsi
	jz 1b
	inc %edi
	push %rsi
	push %rdi
	
        movl %eax, %eax
try:
        movq %gs:0x1000, %rdx
__mcfi_bary_entry:      
        cmpq %rdx, %gs:(%rax)
        jne die # this indirect jump only executes once and cannot fail
        xor %edx,%edx
	jmp *%rax
die:
        hlt
        
