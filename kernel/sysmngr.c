#include "qizo.h"

#define SM_MAJOR 1
#define SM_MINOR 0
#define SM_WORD 32

#define BI32(field) (*(u32 *)((u8 *)qizo_boot() + (field)))
#define BI16(field) (*(u16 *)((u8 *)qizo_boot() + (field)))
#define BI8(field) (*(u8 *)((u8 *)qizo_boot() + (field)))
#define BI64(field) (*(u64 *)((u8 *)qizo_boot() + (field)))

static int word(const char **p, char *out, u32 max)
{
	const char *s = *p;
	u32 n = 0;

	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s)
		return 0;
	while (*s && *s != ' ' && *s != '\t') {
		if (n + 1 < max)
			out[n++] = *s;
		s++;
	}
	out[n] = 0;
	*p = s;
	return (int)n;
}

static void size_line(const char *label, u64 bytes)
{
	qizo_puts("  ");
	qizo_puts(label);
	qizo_pad(label, 12);
	qizo_printf("%lu KiB", bytes / 1024);
	if (bytes >= 1024ULL * 1024ULL)
		qizo_printf("  (%lu MiB)", bytes / (1024ULL * 1024ULL));
	qizo_puts("\r\n");
}

static void free_mem(void)
{
	u64 frames = qizo_pma_total();
	u64 left = qizo_pma_free_count();
	u64 total = frames * QIZO_PAGE_SIZE;
	u64 free_bytes = left * QIZO_PAGE_SIZE;

	qizo_puts("memory\r\n");
	size_line("total", total);
	size_line("free", free_bytes);
	size_line("used", total > free_bytes ? total - free_bytes : 0);
	size_line("held by kernel", qizo_pma_reserved() * QIZO_PAGE_SIZE);
	qizo_printf("  %lu of %lu frames free, %u bytes per frame\r\n", left, frames,
		    (u32)QIZO_PAGE_SIZE);
	qizo_printf("  bios reported %lu KiB of ram\r\n", BI64(QIZO_BI_TOTAL_RAM) / 1024);
}

static void free_vram(void)
{
	u64 window = 128ULL * 1024ULL;
	u64 screen = 80ULL * 25ULL * 2ULL;

	qizo_puts("vram\r\n");
	qizo_puts("  qizo drives the screen as 80x25 colour text, there is no linear\r\n");
	qizo_puts("  framebuffer yet, so the whole aperture is not allocatable\r\n");
	size_line("aperture", window);
	size_line("text screen", screen);
	size_line("free", window - screen);
	qizo_printf("  vga window at %p, text screen at %p\r\n", (u64)0xA0000, (u64)0xB8000);
}

static void free_drive(const char *name)
{
	struct qizo_fat fs;
	u64 sectors = 0;
	u32 drive = 0;

	if (name[0] == 'd' || name[0] == 'D')
		drive = 1;
	else if (name[0] == 'f' || name[0] == 'F')
		drive = 2;
	if (!qizo_ata_drive_sectors(drive, &sectors)) {
		qizo_printf("%s/: no drive attached\r\n", name);
		if (!drive)
			qizo_puts("  the boot drive answers no ata command, so no volume can be read\r\n");
		return;
	}
	qizo_printf("%s/: drive 0x%x, %lu KiB raw\r\n", name, 0x1F0 + drive * 0x180,
		    sectors * 512 / 1024);
	if (!qizo_fat_stat_drive(drive, &fs)) {
		qizo_puts("  no readable boot volume on it\r\n");
		return;
	}
	qizo_printf("  %s volume", fs.type);
	if (fs.label[0])
		qizo_printf(" \"%s\"", fs.label);
	qizo_puts("\r\n");
	size_line("total", fs.total_bytes);
	size_line("free", fs.free_bytes);
	size_line("used", fs.total_bytes > fs.free_bytes ? fs.total_bytes - fs.free_bytes : 0);
	qizo_printf("  %lu of %lu clusters free, %u sectors per cluster, volume starts at lba %lu\r\n",
		    (u64)fs.free_clusters, (u64)fs.clusters, fs.sect_per_clust, fs.start_lba);
}

static void show_usage(void)
{
	qizo_puts("sysmngr -free mem | vram | c/ | d/ | f/\r\n");
	qizo_puts("sysmngr -drivers            probe report for every driver\r\n");
	qizo_puts("sysmngr -net                network card state\r\n");
	qizo_puts("sysmngr -v | -v qizo | -v kernel | -v boot | -v boot stages\r\n");
	qizo_puts("sysmngr -update             replace the running image (not built yet)\r\n");
	qizo_puts("sysmngr -doc                what these flags mean\r\n");
	qizo_printf("sysmngr %u.%u, part of qizo %u.%u.%u\r\n", SM_MAJOR, SM_MINOR,
		    QIZO_VERSION_MAJOR, QIZO_VERSION_MINOR, QIZO_VERSION_PATCH);
}

static void show_doc(void)
{
	qizo_puts("sysmngr reads the running kernel, it never changes anything\r\n");
	qizo_puts("\r\n");
	qizo_puts("-free mem      frames the page allocator can still hand out\r\n");
	qizo_puts("-free vram     how much of the vga aperture the console is not using\r\n");
	qizo_puts("-free c/       free clusters in the volume the system booted from\r\n");
	qizo_puts("-free d/ f/    the same, for the other ata drives in the machine\r\n");
	qizo_puts("-drivers       every driver, whether it probed, and what it found\r\n");
	qizo_puts("-net           the network card qizo can see, its mac and io window\r\n");
	qizo_puts("-v             the version of this command\r\n");
	qizo_puts("-v qizo        the version of the whole operating system\r\n");
	qizo_puts("-v kernel      kernel version plus how much of it is loaded\r\n");
	qizo_puts("-v boot        what the boot left in the handoff block\r\n");
	qizo_puts("-v boot stages the size and address of every boot stage\r\n");
	qizo_puts("-update        reserved for image replacement, see docs/BOOTING.md\r\n");
	qizo_puts("\r\n");
	qizo_puts("sizes are printed in KiB and MiB, both from the same counters\r\n");
}

static void show_update(void)
{
	qizo_printf("sysmngr %u.%u: -update is not in this build\r\n", SM_MAJOR, SM_MINOR);
	qizo_puts("qizo has no writable filesystem and no partition writer, so an update\r\n");
	qizo_puts("is a whole image at the moment: write qizo.img to the disk again.\r\n");
	qizo_puts("The pieces -update will need are a block writer, a second slot for the\r\n");
	qizo_puts("payload, and the payload checksum the boot already verifies.\r\n");
}

static void show_net(void)
{
	u8 mac[6];
	u32 port = 0;
	u32 i;

	qizo_drivers_start();
	qizo_printf("pci: %u devices on bus 0\r\n", qizo_pci_count());
	if (!qizo_net_io_port(&port)) {
		qizo_puts("net: no supported card (qizo knows rtl8139)\r\n");
		return;
	}
	qizo_net_mac(mac);
	qizo_printf("net: rtl8139 at io %p\r\n", (u64)port);
	qizo_puts("  mac ");
	for (i = 0; i < 6; i++) {
		qizo_printf("%x", mac[i] >> 4);
		qizo_printf("%x", mac[i] & 0x0F);
		if (i != 5)
			qizo_puts(":");
	}
	qizo_printf("\r\n  %s\r\n", qizo_net_present() ? "idle, ready for a stack" : "not responding");
}

static void show_version(const char *what)
{
	if (!qizo_strcmp(what, "qizo") || !qizo_strcmp(what, "os")) {
		qizo_printf("qizo %u.%u.%u\r\n", QIZO_VERSION_MAJOR, QIZO_VERSION_MINOR,
			    QIZO_VERSION_PATCH);
		qizo_printf("  built for x86-64, %s long mode, a20 %s, boot drive %x\r\n",
			    BI8(QIZO_BI_LONGMODE) ? "running in" : "not in",
			    BI8(QIZO_BI_A20) ? "on" : "off", BI8(QIZO_BI_DRIVE));
		return;
	}
	if (!qizo_strcmp(what, "kernel")) {
		qizo_printf("kernel %u.%u.%u\r\n", QIZO_VERSION_MAJOR, QIZO_VERSION_MINOR,
			    QIZO_VERSION_PATCH);
		qizo_printf("  text %u KiB of %u KiB image, entry offset %p, at %p\r\n",
			    BI32(QIZO_BI_KERNEL_SIZE) / 1024,
			    BI32(QIZO_BI_KERNEL_TOTAL) / 1024,
			    (u64)BI32(QIZO_BI_KERNEL_ENTRY), (u64)BI32(QIZO_BI_KTEXT));
		return;
	}
	if (!qizo_strcmp(what, "boot")) {
		qizo_printf("boot: handoff version %x, mode %u, error %x\r\n",
			    BI32(QIZO_BI_MAGIC), BI8(QIZO_BI_MODE), BI8(QIZO_BI_ERR));
		qizo_printf("  bootinfo %u bytes at %p, %u memory map entries\r\n",
			    QIZO_BI_SIZE, (u64)0x10000, BI32(QIZO_BI_E820_N));
		return;
	}
	if (!qizo_strcmp(what, "stages") || !qizo_strcmp(what, "boot stages")) {
		qizo_puts("boot stages\r\n");
		qizo_printf("  stage1 at %p, stage2 at %p\r\n", (u64)0x7C00, (u64)0x20000);
		qizo_printf("  payload: lba %u, %u sectors, loaded at %p\r\n",
			    BI32(QIZO_BI_BLOB_LBA), BI16(QIZO_BI_BLOB_SECTORS),
			    (u64)BI32(QIZO_BI_BLOB_PHYS));
		size_line("payload", BI32(QIZO_BI_BLOB_SIZE));
		qizo_printf("  checksum %p, scratch at %p, kernel at %p\r\n",
			    (u64)BI32(QIZO_BI_BLOB_CRC), (u64)BI32(QIZO_BI_SCRATCH),
			    (u64)BI32(QIZO_BI_KTEXT));
		return;
	}
	qizo_printf("sysmngr: no version for %s\r\n", what);
}

void qizo_sysmngr(const char *arg)
{
	char a[SM_WORD];
	char b[SM_WORD];
	const char *p = arg ? arg : "";

	if (!word(&p, a, sizeof(a))) {
		show_usage();
		return;
	}
	if (!qizo_strcmp(a, "-help") || !qizo_strcmp(a, "help") || !qizo_strcmp(a, "-h")) {
		show_usage();
		return;
	}
	if (!qizo_strcmp(a, "-doc")) {
		show_doc();
		return;
	}
	if (!qizo_strcmp(a, "-update")) {
		show_update();
		return;
	}
	if (!qizo_strcmp(a, "-drivers") || !qizo_strcmp(a, "-dev")) {
		qizo_drivers_report();
		return;
	}
	if (!qizo_strcmp(a, "-net")) {
		show_net();
		return;
	}
	if (!qizo_strcmp(a, "-free")) {
		if (!word(&p, b, sizeof(b))) {
			qizo_puts("free: say mem, vram, c/, d/ or f/\r\n");
			return;
		}
		if (!qizo_strcmp(b, "mem") || !qizo_strcmp(b, "memory")) {
			free_mem();
			return;
		}
		if (!qizo_strcmp(b, "vram") || !qizo_strcmp(b, "video")) {
			free_vram();
			return;
		}
		if (!qizo_strcmp(b, "all")) {
			free_mem();
			free_vram();
			free_drive("c/");
			free_drive("d/");
			free_drive("f/");
			return;
		}
		free_drive(b);
		return;
	}
	if (!qizo_strcmp(a, "-v") || !qizo_strcmp(a, "-version")) {
		if (!word(&p, b, sizeof(b))) {
			qizo_printf("sysmngr %u.%u\r\n", SM_MAJOR, SM_MINOR);
			return;
		}
		if (!qizo_strcmp(b, "boot")) {
			if (word(&p, a, sizeof(a)) && !qizo_strcmp(a, "stages")) {
				show_version("stages");
				return;
			}
			show_version("boot");
			return;
		}
		show_version(b);
		return;
	}
	qizo_printf("sysmngr: unknown flag %s, try sysmngr -help\r\n", a);
}
