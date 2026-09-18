ENTRY(_start)
SECTIONS
{
	. = 0xA0000;
	__qizo_base = .;
	.qizo_boot : { *(.qizo_boot) }
	.qizo_pm32 : { *(.qizo_pm32) }
	.qizo_long64 : { *(.qizo_long64) }
	.qizo_aux : { *(.qizo_aux) }
	. = ALIGN(512);
	__qizo_blob_start = .;
	. = ALIGN(512);
	__qizo_blob_end = .;
	__qizo_code_size = __qizo_blob_start - __qizo_base;
	__qizo_total = . - __qizo_base;
	/DISCARD/ : { *(.note.GNU-stack) *(.comment) *(.eh_frame) }
}
