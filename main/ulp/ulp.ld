ENTRY(ulp_riscv_entry)

MEMORY {
    rtc_slow_seg : ORIGIN = 0x50000000, LENGTH = 0x2000
}

SECTIONS {
    .text : {
        *(.text)
    } > rtc_slow_seg
    
    .bss (NOLOAD) : {
        *(.bss)
    } > rtc_slow_seg
    
    .data : {
        *(.data)
        *(.rodata)
    } > rtc_slow_seg
    
    .stack (NOLOAD) : {
        . = ALIGN(4);
        stack_start = .;
        . += 512; /* 512字节堆栈 */
        stack_end = .;
    } > rtc_slow_seg
}