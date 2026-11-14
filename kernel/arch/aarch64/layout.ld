ENTRY(_start)

PHDRS {
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    /* Leave the bottom of virt RAM available for the device tree. */
    . = 0x40200000;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    .data ALIGN(4096) : { *(.data .data.*) } :data
    .bss ALIGN(4096) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        . = ALIGN(16);
        __bss_end = .;
    } :data
}
