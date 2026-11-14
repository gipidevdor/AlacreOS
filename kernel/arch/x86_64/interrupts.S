.text
.macro enter_context handler
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
    and $-16, %rsp
    call \handler
    jmp restore_context
.endm

.global timer_vector
.type timer_vector, @function
timer_vector:
    testb $3, 8(%rsp)
    jnz identity_timer_vector
    enter_context platform_timer_interrupt
.size timer_vector, . - timer_vector

.global completion_vector
.type completion_vector, @function
completion_vector:
    enter_context platform_completion_interrupt
.size completion_vector, . - completion_vector

restore_context:
    mov %rax, %rsp
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

.global spurious_vector
.type spurious_vector, @function
spurious_vector:
    iretq
.size spurious_vector, . - spurious_vector

.global unexpected_vector
.type unexpected_vector, @function
unexpected_vector:
    cli
1:
    hlt
    jmp 1b
.size unexpected_vector, . - unexpected_vector
