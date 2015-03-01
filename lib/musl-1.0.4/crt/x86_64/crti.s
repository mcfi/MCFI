        .section .init
        .type _init,@function
        .global _init
_init:
	push %rax

        .section .fini
        .type _fini,@function
        .global _fini
_fini:
	push %rax
