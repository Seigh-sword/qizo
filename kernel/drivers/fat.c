#include "qizo.h"

#define FAT_SCAN_LBAS 96

struct qizo_bpb {
	u8 jump[3];
	u8 oem[8];
	u16 bytssec;
	u8 spc;
	u16 reserved;
	u8 fats;
	u16 rootent;
	u16 mbr_sects;
	u8 media;
	u16 spf;
	u16 tracks;
	u16 heads;
	u32 hidden;
	u32 big_sects;
	u8 drive;
	u8 reserved1;
	u8 extver;
	u8 label[11];
	u8 sysid[8];
} __attribute__((packed));

static u8 sector[512] __attribute__((aligned(16)));
static u8 *fatbuf;
static struct qizo_fat fs;
static int fs_ready;

static int bpb_sane(struct qizo_bpb *b)
{
	if (sector[510] != 0x55 || sector[511] != 0xAA)
		return 0;
	if (b->bytssec != 512)
		return 0;
	if (!b->spc || (b->spc & (b->spc - 1)))
		return 0;
	if (b->spc > 128)
		return 0;
	if (b->fats < 1 || b->fats > 4)
		return 0;
	if (!b->spf || !b->rootent)
		return 0;
	if (!b->mbr_sects && !b->big_sects)
		return 0;
	if (b->media != 0xF0 && b->media != 0xF8 && b->media != 0x00)
		return 0;
	return 1;
}

static int fat_entry_12(u8 *f, u32 i)
{
	u32 v;

	v = (u32)f[i + i / 2];
	v |= (u32)f[i + i / 2 + 1] << 8;
	if (i & 1)
		v >>= 4;
	else
		v &= 0x0FFF;
	return (int)v;
}

static int fat_entry_16(u8 *f, u32 i)
{
	return (int)(((u16)f[i * 2]) | ((u16)f[i * 2 + 1] << 8));
}

static u32 count_free(int is12, u8 *f, u32 bytes, u32 clusters)
{
	u32 i;
	u32 free_count = 0;
	u32 limit = is12 ? (bytes * 2 / 3) : (bytes / 2);

	if (limit > clusters + 2)
		limit = clusters + 2;
	for (i = 2; i < limit; i++) {
		int v = is12 ? fat_entry_12(f, i) : fat_entry_16(f, i);

		if (v == 0)
			free_count++;
	}
	if (clusters > limit - 2)
		free_count += clusters - (limit - 2);
	return free_count;
}

static int label_text(u8 *src, char *dst, u32 n)
{
	u32 i;
	u32 used = 0;

	for (i = 0; i < n; i++) {
		char c = (char)src[i];

		if (c < 32 || c > 126)
			c = ' ';
		dst[i] = c;
		if (c != ' ')
			used = i + 1;
	}
	dst[used] = 0;
	return (int)used;
}

static int read_sectors(u32 drive, u64 lba, u32 count, void *dst)
{
	return qizo_ata_read_drive(drive, lba, count, dst);
}

static int scan_drive(u32 drive, u64 first, struct qizo_fat *out)
{
	struct qizo_bpb *b = (struct qizo_bpb *)sector;
	u64 total_sects;
	u32 root_sects;
	u32 fat_sects;
	u32 data_start;
	u32 bytes;
	u32 clusters;
	int is12;
	u64 lba;
	u32 i;

	for (i = 0; i < FAT_SCAN_LBAS; i++) {
		lba = first + i;
		if (!read_sectors(drive, lba, 1, sector))
			continue;
		if (!bpb_sane(b))
			continue;
		total_sects = b->mbr_sects ? b->mbr_sects : b->big_sects;
		if (total_sects > 0x0FFFFFFFULL)
			total_sects = 0x0FFFFFFFULL;
		root_sects = ((u32)b->rootent * 32 + 511) / 512;
		fat_sects = (u32)b->fats * b->spf;
		data_start = b->reserved + fat_sects + root_sects;
		if ((u64)data_start >= total_sects)
			continue;
		clusters = (u32)((total_sects - data_start) / b->spc);
		if (clusters < 2)
			continue;
		is12 = clusters < 4085;
		bytes = (u32)b->spf * 512;
		if (bytes > 256 * 1024)
			bytes = 256 * 1024;
		if (!fatbuf)
			fatbuf = (u8 *)qizo_page_alloc((bytes + 4095) / 4096);
		if (!fatbuf)
			return 0;
		if (!read_sectors(drive, first + b->reserved, bytes / 512, fatbuf)) {
			u32 done = 0;

			while (done < bytes / 512) {
				u32 chunk = bytes / 512 - done;

				if (chunk > 16)
					chunk = 16;
				if (!read_sectors(drive, first + b->reserved + done, chunk,
						  fatbuf + done * 512))
					break;
				done += chunk;
			}
			if (!done)
				return 0;
			bytes = done * 512;
		}
		out->present = 1;
		out->start_lba = (u64)lba;
		out->sect_per_clust = b->spc;
		out->clusters = clusters;
		out->free_clusters = count_free(is12, fatbuf, bytes, clusters);
		out->total_bytes = (u64)clusters * b->spc * 512;
		out->free_bytes = (u64)out->free_clusters * b->spc * 512;
		if (out->free_bytes > out->total_bytes)
			out->free_bytes = out->total_bytes;
		qizo_memcpy(out->type, is12 ? "FAT12   " : "FAT16   ", 8);
		out->type[8] = 0;
		label_text(b->label, out->label, 11);
		out->label[11] = 0;
		return 1;
	}
	return 0;
}

static int fat_for(u32 drive, struct qizo_fat *out)
{
	qizo_drivers_start();
	out->present = 0;
	out->label[0] = 0;
	qizo_memcpy(out->type, "none    ", 8);
	out->total_bytes = 0;
	out->free_bytes = 0;
	out->clusters = 0;
	out->free_clusters = 0;
	out->start_lba = 0;
	if (!qizo_ata_present())
		return 0;
	if (drive > 1)
		return 0;
	{
		u8 *bi = (u8 *)qizo_boot();
		u32 blob_lba = *(u32 *)(bi + QIZO_BI_BLOB_LBA);
		u32 blob_sects = *(u16 *)(bi + QIZO_BI_BLOB_SECTORS);
		if (scan_drive(drive, blob_lba + blob_sects, out))
			return 1;
		if (scan_drive(drive, 63, out))
			return 1;
		if (scan_drive(drive, 2048, out))
			return 1;
	}
	return 0;
}

int qizo_fat_probe(void)
{
	if (!qizo_ata_probe())
		return 0;
	if (fs_ready)
		return fs_ready;
	fs_ready = fat_for(0, &fs);
	return fs_ready;
}

void qizo_fat_start(void)
{
	if (!fs_ready)
		fs_ready = fat_for(0, &fs);
}

const char *qizo_fat_detail(void)
{
	return fs.present ? "boot volume readable" : "no boot volume";
}

int qizo_fat_stat(struct qizo_fat *out)
{
	return fat_for(0, out);
}

int qizo_fat_stat_drive(u32 drive, struct qizo_fat *out)
{
	return fat_for(drive, out);
}
