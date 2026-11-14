.text
.global x86_domain_enter
.type x86_domain_enter, @function
x86_domain_enter:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rsi)  # TSS RSP0 points to the saved kernel registers.
    mov %rdi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    iretq
.size x86_domain_enter, . - x86_domain_enter

.macro identity_vector symbol, kind, error=0
.global \symbol
.type \symbol, @function
\symbol:
    .if \error
        add $8, %rsp
    .endif
    testb $3, 8(%rsp)
    jz unexpected_vector
    cld
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    mov $\kind, %esi
    and $-16, %rsp
    call x86_domain_event
    mov %rax, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.size \symbol, . - \symbol
.endm

identity_vector identity_syscall_vector, 0
identity_vector identity_page_fault_vector, 1, 1
identity_vector identity_protection_vector, 2, 1
identity_vector identity_invalid_opcode_vector, 2
identity_vector identity_no_fp_vector, 2
identity_vector identity_debug_vector, 2
identity_vector identity_breakpoint_vector, 2
identity_vector identity_overflow_vector, 2
identity_vector identity_divide_vector, 2
identity_vector identity_segment_fault_vector, 2, 1
identity_vector identity_timer_vector, 3

.section .note.GNU-stack,"",@progbits
