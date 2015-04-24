        .section .init
        .type _init,@function
        .global _init
        .align 16, 0x90
_init:
	push %rax

        .section .fini
        .type _fini,@function
        .global _fini
        .align 16, 0x90
_fini:
	push %rax
