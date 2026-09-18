ENTRY(_start)
SECTIONS
{
	. = 0x7C00;
	__qizo_base = .;
	.qizo_boot : { *(.qizo_boot) }
	__qizo_boot_size = . - __qizo_base;
	.qizo_data : { *(.qizo_data) }
	__qizo_data_end = . - __qizo_base;
	. = 0x7C00 + 510;
	.qizo_sig : { SHORT(0xAA55) }
	__qizo_total = . - __qizo_base;
	/DISCARD/ : { *(.note.GNU-stack) *(.comment) *(.eh_frame) }
}
