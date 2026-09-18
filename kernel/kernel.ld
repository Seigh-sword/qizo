ENTRY(qizo_kernel_entry)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = 0x100000;
	__qizo_phys_base = .;

	.text : {
		*(.qizo_text)
		*(.text .text.*)
	} :text

	. = ALIGN(16);
	.rodata : {
		*(.rodata .rodata.*)
	} :text

	.data : {
		*(.qizo_data)
		*(.data .data.*)
	} :data
	__qizo_image_end = .;

	.bss : {
		*(.bss .bss.*)
		*(.qizo_bss)
		*(COMMON)
		. = ALIGN(16);
	} :data

	__qizo_bss_start = ADDR(.bss);
	__qizo_bss_end = .;

	/DISCARD/ : {
		*(.note.gnu.property)
		*(.note.GNU-stack)
		*(.comment)
		*(.eh_frame .eh_frame.*)
		*(.rela .rela*)
		*(.interp)
	}
}
