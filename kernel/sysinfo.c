#include "qizo.h"

static const struct {
	u64 bit;
	const char *name;
} feat_table[] = {
	{ QIZO_FEAT_LAHF, "LAHF-SAHF" },
	{ QIZO_FEAT_SSE2, "SSE2" },
	{ QIZO_FEAT_SSE3, "SSE3" },
	{ QIZO_FEAT_SSSE3, "SSSE3" },
	{ QIZO_FEAT_SSE41, "SSE4.1" },
	{ QIZO_FEAT_SSE42, "SSE4.2" },
	{ QIZO_FEAT_POPCNT, "POPCNT" },
	{ QIZO_FEAT_AVX, "AVX" },
	{ QIZO_FEAT_AVX2, "AVX2" },
	{ QIZO_FEAT_AVX512F, "AVX512F" },
	{ QIZO_FEAT_BMI2, "BMI2" },
	{ QIZO_FEAT_FSGSBASE, "FSGSBASE" },
	{ QIZO_FEAT_ERMS, "ERMS" },
	{ QIZO_FEAT_1GB, "PAGE1GB" },
	{ QIZO_FEAT_SMEP, "SMEP" },
	{ QIZO_FEAT_SMAP, "SMAP" },
};

static void human_size(u64 bytes)
{
	if (bytes >= 1024ULL * 1024 * 1024)
		qizo_printf("%u.%u GiB", (u32)(bytes / (1024ULL * 1024 * 1024)),
			      (u32)((bytes % (1024ULL * 1024 * 1024)) * 10 / (1024ULL * 1024 * 1024)));
	else if (bytes >= 1024ULL * 1024)
		qizo_printf("%u.%u MiB", (u32)(bytes / (1024ULL * 1024)),
			    (u32)((bytes % (1024ULL * 1024)) * 10 / (1024ULL * 1024)));
	else
		qizo_printf("%u KiB", (u32)(bytes / 1024));
}

void qizo_banner(void)
{
	struct qizo_bootinfo *info = qizo_boot();

	qizo_puts("\r\n");
	qizo_puts("        ___         _       \r\n");
	qizo_puts("       / _(_)__ _  (_)___ __ \r\n");
	qizo_puts("      /  // / _ `/ / (_-</ _ \\\r\n");
	qizo_puts("     /_///_\\_, /_/_//_/\\_,_/\r\n");
	qizo_puts("          /___/     \r\n");
	qizo_printf("qizo %d.%d.%d  x86-64  freestanding\r\n", QIZO_VERSION_MAJOR,
		    QIZO_VERSION_MINOR, QIZO_VERSION_PATCH);
	qizo_printf("kernel image %u KiB at %p, boot mode %u, bootver %x\r\n",
		    info ? (u32)(info->kernel_total / 1024) : 0,
		    info ? (void *)(ulong)info->ktext : (void *)0,
		    info ? info->mode : 0, (u32)(info ? info->cpuid_max : 0));
}

void qizo_cpuinfo(void)
{
	int i;
	int first = 1;

	qizo_printf("cpu   : %s\r\n", qizo_cpu_brand());
	qizo_printf("vendor: %s  family %u  model %u  stepping %u\r\n", qizo_cpu_vendor(),
		    qizo_cpu_family(), qizo_cpu_model(), 0);
	qizo_puts("feats : ");
	for (i = 0; i < (int)(sizeof(feat_table) / sizeof(feat_table[0])); i++) {
		if (qizo_cpu_has(feat_table[i].bit)) {
			if (!first)
				qizo_puts(" ");
			qizo_puts(feat_table[i].name);
			first = 0;
		}
	}
	if (first)
		qizo_puts("baseline");
	qizo_printf("\r\nclock : %u MHz nominal\r\n", (u32)(qizo_cycles_per_ms() / 1000));
}

void qizo_sysinfo(void)
{
	struct qizo_bootinfo *info = qizo_boot();
	u32 i;
	u64 ram, free_frames;

	qizo_banner();
	qizo_cpuinfo();
	ram = info ? info->total_ram : 0;
	free_frames = qizo_pma_free_count();
	qizo_printf("mem   : %u KiB usable, %u pages free, %u pages in use\r\n",
		    (u32)(ram / 1024), (u32)free_frames, (u32)qizo_pma_reserved());
	qizo_printf("map   : top %u KiB, frames %u\r\n",
		    (u32)(qizo_pma_max_phys() / 1024), (u32)qizo_pma_total());
	qizo_puts("e820  :\r\n");
	if (info) {
		for (i = 0; i < info->map_count && i < QIZO_E820_MAX; i++) {
			struct qizo_e820 *e = &info->map[i];
			const char *t = "reserved";

			if (e->type == 1)
				t = "usable";
			else if (e->type == 2)
				t = "acpi recl";
			else if (e->type == 3)
				t = "acpi data";
			else if (e->type == 4)
				t = "bad";
			qizo_printf("  %x-%x %s (", (u32)e->base, (u32)(e->base + e->size), t);
			human_size(e->size);
			qizo_printf(")\r\n");
		}
	}
	qizo_printf("irqs  : keyboard %u, unexpected %u\r\n", (u32)qizo_keyboard_irqs(),
		    (u32)qizo_spurious_irqs());
	qizo_puts("status: kernel online, interrupts live, shell ready\r\n");
}

void qizo_memstat(void)
{
	u32 i;
	struct qizo_bootinfo *info = qizo_boot();

	qizo_printf("pma   : %u frames total, %u free, %u allocated\r\n",
		    (u32)qizo_pma_total(), (u32)qizo_pma_free_count(), (u32)qizo_pma_reserved());
	qizo_printf("top   : %u KiB addressable\r\n", (u32)(qizo_pma_max_phys() / 1024));
	if (!info)
		return;
	qizo_puts("alloc demo:\r\n");
	for (i = 0; i < 4; i++) {
		u64 p = qizo_pma_alloc();

		if (!p) {
			qizo_puts("  out of frames\r\n");
			return;
		}
		qizo_memset_sse2((void *)p, 0x5A + i, QIZO_PAGE_SIZE);
		qizo_printf("  page %u at %p filled with %02x\r\n", i, (void *)p, 0x5A + i);
	}
	qizo_pma_free(0);
}
