#include "qizo.h"

#define ATA_CTRL 0x1F0
#define ATA_STATUS 0x1F7
#define ATA_ALT 0x3F6
#define ATA_SPIN 400000

static int drive_ok[2];
static u64 drive_sectors[2];
static char drive_model[2][41];
static u32 ata_reads;
static u32 ata_errors;
static u64 ata_blocks;
static int ata_live;

static u16 ata_inw(u16 port)
{
	u16 v;

	__asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

static u8 ata_status(void)
{
	return qizo_inb(ATA_ALT);
}

static int ata_wait(u8 want, u8 avoid, u32 spins)
{
	u32 i;

	for (i = 0; i < spins; i++) {
		u8 st = ata_status();

		if (st == 0xFF)
			return -1;
		if (st & avoid)
			continue;
		if ((st & want) == want)
			return 1;
	}
	return 0;
}

static void ata_drain(void)
{
	qizo_inb(0x1F1);
	qizo_inb(0x1F2);
	qizo_inb(0x1F3);
	qizo_inb(0x1F4);
	qizo_inb(0x1F5);
	qizo_inb(0x1F6);
}

static void ata_select(u16 drv, u32 lba_hi)
{
	qizo_outb(0x1F6, 0xE0 | (u8)(drv << 4) | (u8)((lba_hi >> 24) & 0x0F));
}

static int ata_identify(u16 drv, u64 *sectors, char *model)
{
	u16 id[256];
	u32 i;
	u8 st;
	u64 total;

	ata_select(drv, 0);
	ata_drain();
	qizo_outb(0x1F2, 0);
	qizo_outb(0x1F3, 0);
	qizo_outb(0x1F4, 0);
	qizo_outb(0x1F5, 0);
	qizo_outb(ATA_STATUS, 0xEC);
	st = ata_status();
	if (st == 0xFF)
		return 0;
	if (st == 0)
		return 0;
	if (ata_wait(0x58, 0x80, ATA_SPIN) != 1) {
		qizo_inb(0x1F4);
		qizo_inb(0x1F5);
		return 0;
	}
	for (i = 0; i < 256; i++)
		id[i] = ata_inw(ATA_CTRL);
	if (id[0] & 0x8000)
		return 0;
	total = (u64)id[60] | ((u64)id[61] << 32);
	if (!total || total > 0x0FFFFFFFULL)
		total = (u64)id[1] | ((u64)id[3] << 16) | ((u64)id[6] << 32);
	if (total > 0x0FFFFFFFULL)
		total = 0x0FFFFFFFULL;
	*sectors = total;
	for (i = 0; i < 40; i++)
		model[i] = (char)((id[27 + (i >> 1)] >> (i & 1 ? 8 : 0)) & 0xFF);
	model[40] = 0;
	for (i = 0; i < 40; i++) {
		if (model[i] < 32 || model[i] > 126)
			model[i] = ' ';
	}
	while (model[0] == ' ') {
		for (i = 0; i < 40; i++)
			model[i] = model[i + 1];
	}
	for (i = 0; i < 41; i++) {
		if (!model[i] || model[i] == ' ') {
			model[i] = 0;
			break;
		}
	}
	return 1;
}

int qizo_ata_probe(void)
{
	if (qizo_inb(ATA_STATUS) == 0xFF && qizo_inb(0x177) == 0xFF)
		return 0;
	return 1;
}

void qizo_ata_start(void)
{
	u16 drv;

	for (drv = 0; drv < 2; drv++) {
		drive_model[drv][0] = 0;
		if (ata_identify(drv, &drive_sectors[drv], drive_model[drv])) {
			drive_ok[drv] = 1;
			ata_live = 1;
		} else {
			drive_sectors[drv] = 0;
		}
	}
	ata_select(0, 0);
}

int qizo_ata_present(void)
{
	qizo_drivers_start();
	return ata_live;
}

int qizo_ata_drive_count(void)
{
	return drive_ok[0] + drive_ok[1];
}

u64 qizo_ata_sectors(void)
{
	return drive_sectors[0];
}

const char *qizo_ata_model(void)
{
	return drive_model[0][0] ? drive_model[0] : "ata disk";
}

int qizo_ata_drive_sectors(u32 drv, u64 *out)
{
	if (drv > 1 || !drive_ok[drv])
		return 0;
	*out = drive_sectors[drv];
	return 1;
}

const char *qizo_ata_drive_model(u32 drv)
{
	if (drv > 1 || !drive_ok[drv])
		return "";
	return drive_model[drv];
}

int qizo_ata_read_drive(u32 drive, u64 lba, u32 count, void *dst)
{
	u16 *out = (u16 *)dst;
	u32 s;
	u32 i;

	if (drive > 1 || !drive_ok[drive])
		return 0;
	if (!count || count > 16)
		return 0;
	if (lba + count > drive_sectors[drive])
		return 0;
	ata_select((u16)drive, (u32)lba);
	qizo_outb(0x1F2, (u8)count);
	qizo_outb(0x1F3, (u8)lba);
	qizo_outb(0x1F4, (u8)(lba >> 8));
	qizo_outb(0x1F5, (u8)(lba >> 16));
	qizo_outb(ATA_STATUS, 0x20);
	for (s = 0; s < count; s++) {
		if (ata_wait(0x58, 0x01 | 0x80, ATA_SPIN) != 1) {
			ata_errors++;
			return 0;
		}
		for (i = 0; i < 256; i++)
			*out++ = ata_inw(ATA_CTRL);
	}
	ata_reads++;
	ata_blocks += count;
	return 1;
}

int qizo_ata_read(u64 lba, u32 count, void *dst)
{
	return qizo_ata_read_drive(0, lba, count, dst);
}

u64 qizo_ata_reads(void)
{
	return ata_reads;
}

u64 qizo_ata_blocks(void)
{
	return ata_blocks;
}

u32 qizo_ata_errors(void)
{
	return ata_errors;
}

const char *qizo_ata_detail(void)
{
	static const char *none = "no ata drive";

	if (!drive_ok[0])
		return none;
	return drive_model[0][0] ? drive_model[0] : "ata disk";
}
