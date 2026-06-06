.intel_syntax noprefix

.section .text

.global x86_64_load_gdt
.type x86_64_load_gdt, @function
x86_64_load_gdt:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    push 0x08
    lea rax, [rip + 1f]
    push rax
    .byte 0x48, 0xcb
1:
    ret

.global x86_64_load_idt
.type x86_64_load_idt, @function
x86_64_load_idt:
    lidt [rdi]
    ret

.global x86_64_load_tss
.type x86_64_load_tss, @function
x86_64_load_tss:
    ltr di
    ret

.global x86_64_read_cr3
.type x86_64_read_cr3, @function
x86_64_read_cr3:
    mov rax, cr3
    ret

.global x86_64_write_cr3
.type x86_64_write_cr3, @function
x86_64_write_cr3:
    mov cr3, rdi
    ret

.global arch_context_save
.type arch_context_save, @function
arch_context_save:
    lea rax, [rsp + 8]
    mov [rdi + 0], rax
    mov [rdi + 8], rbp
    mov [rdi + 16], rbx
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    mov rax, [rsp]
    mov [rdi + 56], rax
    xor eax, eax
    ret

.global arch_context_restore
.type arch_context_restore, @function
arch_context_restore:
    mov rsp, [rdi + 0]
    mov rbp, [rdi + 8]
    mov rbx, [rdi + 16]
    mov r12, [rdi + 24]
    mov r13, [rdi + 32]
    mov r14, [rdi + 40]
    mov r15, [rdi + 48]
    mov eax, esi
    jmp [rdi + 56]

.global x86_64_enter_user
.type x86_64_enter_user, @function
x86_64_enter_user:
    mov ax, 0x1b
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push 0x1b
    push rsi
    push 0x202
    push 0x23
    push rdi
    iretq

.macro ISR_NOERR vector
.global isr\vector
.type isr\vector, @function
isr\vector:
    push 0
    push \vector
    jmp isr_common
.endm

.macro ISR_ERR vector
.global isr\vector
.type isr\vector, @function
isr\vector:
    push \vector
    jmp isr_common
.endm

.macro IRQ_NOERR vector
.global isr\vector
.type isr\vector, @function
isr\vector:
    push 0
    push \vector
    jmp irq_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

IRQ_NOERR 32
IRQ_NOERR 33
IRQ_NOERR 34
IRQ_NOERR 35
IRQ_NOERR 36
IRQ_NOERR 37
IRQ_NOERR 38
IRQ_NOERR 39
IRQ_NOERR 40
IRQ_NOERR 41
IRQ_NOERR 42
IRQ_NOERR 43
IRQ_NOERR 44
IRQ_NOERR 45
IRQ_NOERR 46
IRQ_NOERR 47
ISR_NOERR 128

.type isr_common, @function
isr_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    mov rbp, rsp
    and rsp, -16
    call exception_dispatch
    mov rsp, rbp

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

.type irq_common, @function
irq_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    mov rbp, rsp
    and rsp, -16
    call irq_dispatch
    mov rsp, rbp

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

.section .note.GNU-stack,"",@progbits
