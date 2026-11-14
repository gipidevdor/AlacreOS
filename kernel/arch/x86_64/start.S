.section .note.Xen,"a",@note
.balign 4
    .long 4, 4, 18             # XEN_ELFNOTE_PHYS32_ENTRY
    .asciz "Xen"
    .long _start
.balign 4

.section .text.boot,"ax"
.global _start
.type _start, @function
.code32
_start:
    cli
    cld
    lgdt gdt_pointer

    # Clear BSS before creating the initial page tables or using the stack.
    mov $__bss_start, %edi
    mov $__bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    # Identity-map the first 2 MiB using a single large page.
    mov $(pdpt + 3), %eax
    mov %eax, pml4
    mov $(page_directory + 3), %eax
    mov %eax, pdpt
    movl $0x83, page_directory

    mov %cr4, %eax
    or $(1 << 5), %eax         # Physical Address Extension
    mov %eax, %cr4
    mov $pml4, %eax
    mov %eax, %cr3
    mov $0xc0000080, %ecx      # EFER
    rdmsr
    or $(1 << 8), %eax         # Long-mode enable
    wrmsr
    mov %cr0, %eax
    or $(1 << 31), %eax        # Paging
    mov %eax, %cr0
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    mov $0x10, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    lea stack_top(%rip), %rsp
    call kernel_main
1:
    hlt
    jmp 1b
.size _start, . - _start

.section .rodata.boot,"a"
.balign 8
gdt:
    .quad 0
    .quad 0x00af9a000000ffff   # 64-bit code
    .quad 0x00cf92000000ffff   # Data
gdt_pointer:
    .word gdt_pointer - gdt - 1
    .long gdt

.section .bss.boot,"aw",@nobits
.balign 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
page_directory:
    .skip 4096
.balign 16
    .skip 16384
stack_top:
