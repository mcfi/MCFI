.global __vfork
.weak vfork
.type __vfork,@function
.type vfork,@function
__vfork:
vfork:
	pop %rdx
	mov $58,%eax
        movb $0x1, %fs:0x18 # raise the in-syscall flag 
	syscall
        movq %fs:0x10, %r11
        addq $1, %r11
        movq %r11, %fs:0x10
        movb $0x0, %fs:0x18
	push %rdx
	mov %rax,%rdi
	jmp __syscall_ret
