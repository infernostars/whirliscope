.intel_syntax noprefix

.section .text.start,"ax"
.global _start
.type _start, @function
_start:
    call app_main
    mov rdi, rax
    mov rax, 2
    int 0x80
1:
    hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
