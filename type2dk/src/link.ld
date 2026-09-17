ENTRY(Reset_Handler)
MEMORY {
 FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 64K
 RAM (rwx) : ORIGIN = 0x04000400, LENGTH = 0x7C00
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM) - 32;
SECTIONS {
 .vectors : { KEEP(*(.vectors)) } > FLASH
 .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
 .ARM.exidx : { *(.ARM.exidx*) } > FLASH
 .data : { _data_start = .; *(.data*) . = ALIGN(4); _data_end = .; } > RAM AT> FLASH
 _data_load = LOADADDR(.data);
 .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
 /DISCARD/ : { *(.comment*) *(.ARM.attributes*) }
 ASSERT(_bss_end < _stack_top - 2048, "RAM overflow")
}
