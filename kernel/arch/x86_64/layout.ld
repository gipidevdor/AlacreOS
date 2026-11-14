ENTRY(_start)

PHDRS {
    note PT_NOTE FLAGS(4);
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x100000;
    .note : { KEEP(*(.note.Xen)) } :code :note
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    .data ALIGN(4096) : { *(.data .data.*) } :data
    .bss ALIGN(4096) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        __bss_end = .;
    } :data
    ASSERT(__bss_end <= 0x200000, "Bootstrap image exceeds the initial identity mapping")
}
