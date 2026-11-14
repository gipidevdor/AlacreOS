ENTRY(_start)

PHDRS {
    note PT_NOTE FLAGS(4);
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x100000;
    __image_start = .;
    .note : { KEEP(*(.note.Xen)) } :code :note
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
    __kernel_text_end = .;
    .rodata : { *(.rodata .rodata.*) } :code
    .identity_code ALIGN(4096) : {
        __identity_code = .;
        *(.identity_text .identity_rodata)
        . = ALIGN(4096);
        __identity_code_end = .;
    } :code
    .identity_client ALIGN(4096) : {
        __identity_client = .;
        *(.identity_client_text .identity_client_rodata)
        . = ALIGN(4096);
        __identity_client_end = .;
    } :code
    .data ALIGN(4096) : { *(.data .data.*) } :data
    .identity_state ALIGN(4096) : {
        __identity_state = .;
        *(.identity_state)
        . = ALIGN(4096);
        __identity_state_end = .;
    } :data
    .bss ALIGN(4096) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        __bss_end = .;
    } :data
    __image_end = .;
    ASSERT(__bss_end <= 0x200000, "Bootstrap image exceeds the initial identity mapping")
}
