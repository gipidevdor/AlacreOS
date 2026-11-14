.text
.global arm_domain_enter
.type arm_domain_enter, %function
arm_domain_enter:
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov x30, x0
    ldp x1, x2, [x30, #248]  // User SP and return PC.
    msr sp_el0, x1
    msr elr_el1, x2
    ldr x1, [x30, #264]      // EL0t. IRQ enabled, D/A/F masked.
    msr spsr_el1, x1
    ldp x0, x1, [x30, #0]
    ldp x2, x3, [x30, #16]
    ldp x4, x5, [x30, #32]
    ldp x6, x7, [x30, #48]
    ldp x8, x9, [x30, #64]
    ldp x10, x11, [x30, #80]
    ldp x12, x13, [x30, #96]
    ldp x14, x15, [x30, #112]
    ldp x16, x17, [x30, #128]
    ldp x18, x19, [x30, #144]
    ldp x20, x21, [x30, #160]
    ldp x22, x23, [x30, #176]
    ldp x24, x25, [x30, #192]
    ldp x26, x27, [x30, #208]
    ldp x28, x29, [x30, #224]
    ldr x30, [x30, #240]
    eret
.size arm_domain_enter, . - arm_domain_enter

.macro save_domain
    stp x0, x1, [sp, #-272]!
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x1, sp_el0
    mrs x2, elr_el1
    stp x1, x2, [sp, #248]
    mrs x1, spsr_el1
    str x1, [sp, #264]
    mov x0, sp
.endm

.global domain_sync_vector
domain_sync_vector:
    save_domain
    mrs x1, esr_el1
    mrs x2, far_el1
    bl arm_domain_sync
    b return_to_kernel

.global domain_irq_vector
domain_irq_vector:
    save_domain
    bl arm_domain_irq

return_to_kernel:
    add sp, sp, #272
    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96
    ret

.section .note.GNU-stack,"",%progbits
