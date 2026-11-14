.section .text.boot,"ax"
.global _start
.type _start, %function
_start:
    msr daifset, #0xf

    ldr x0, =__bss_start
    ldr x1, =__bss_end
1:
    cmp x0, x1
    b.hs 2f
    stp xzr, xzr, [x0], #16
    b 1b

2:
    ldr x0, =stack_top
    mov sp, x0
    bl kernel_main
3:
    wfi
    b 3b
.size _start, . - _start

.section .bss.stack,"aw",%nobits
.balign 16
    .skip 16384
stack_top:
