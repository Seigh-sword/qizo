#include "qizo.h"

#define PMA_MAX_FRAMES (QIZO_PMAP_PAGES * 512)
#define PMA_MIN_USABLE (24ULL * 1024 * 1024)

static u64 bitmap[PMA_MAX_FRAMES / 64 + 2];
static u64 total_frames;
static u64 free_frames;
static u64 used_frames;
static u64 max_phys;
static u64 hint;

static inline u64 phys_to_virt(u64 phys)
{
	return phys + QIZO_HHDM_VIRT;
}

static int frame_ok(u64 frame)
{
	if (frame >= total_frames)
		return 0;
	return (bitmap[frame >> 6] & (1ULL << (frame & 63))) != 0;
}

static void mark(u64 frame, int good)
{
	if (frame >= total_frames)
		return;
	if (good)
		bitmap[frame >> 6] |= 1ULL << (frame & 63);
	else
		bitmap[frame >> 6] &= ~(1ULL << (frame & 63));
}

static void mark_range(u64 base, u64 len, int good)
{
	u64 first = base & ~(QIZO_PAGE_SIZE - 1);
	u64 last = (base + len + QIZO_PAGE_SIZE - 1) & ~(QIZO_PAGE_SIZE - 1);
	u64 frame;

	for (frame = first / QIZO_PAGE_SIZE; frame * QIZO_PAGE_SIZE < last; frame++)
		mark(frame, good);
}

void qizo_pma_init(struct qizo_bootinfo *info)
{
	u32 count, i;
	u64 top = 0;
	u64 kernel_end;
	extern char __qizo_phys_base[];
	extern char __qizo_image_end[];
	extern char __qizo_bss_end[];

	total_frames = PMA_MAX_FRAMES;
	count = info ? info->map_count : 0;
	if (count > QIZO_E820_MAX)
		count = QIZO_E820_MAX;
	for (i = 0; i < count; i++) {
		struct qizo_e820 *e = &info->map[i];
		u64 end = e->base + e->size;

		if (e->type != 1)
			continue;
		if (e->base >= QIZO_PMAP_PAGES * 0x200000ULL)
			continue;
		if (end > top)
			top = end;
	}
	if (!top && info && info->total_ram)
		top = info->total_ram;
	if (top < PMA_MIN_USABLE)
		top = PMA_MIN_USABLE;
	if (top > total_frames * QIZO_PAGE_SIZE)
		top = total_frames * QIZO_PAGE_SIZE;
	max_phys = top;
	total_frames = top / QIZO_PAGE_SIZE;
	qizo_memset_sse2(bitmap, 0, sizeof(bitmap));
	for (i = 0; i < count; i++) {
		struct qizo_e820 *e = &info->map[i];
		u64 base, len;

		if (e->type != 1)
			continue;
		base = e->base & ~(QIZO_PAGE_SIZE - 1);
		len = (e->base + e->size) & ~(QIZO_PAGE_SIZE - 1);
		if (len <= base)
			continue;
		if (len > top)
			len = top;
		if (base >= top)
			continue;
		mark_range(base, len - base, 1);
	}
	mark_range(0, 1024 * 1024, 0);
	kernel_end = (u64)__qizo_bss_end - (u64)__qizo_phys_base + (u64)__qizo_phys_base;
	if (info && info->kernel_total)
		kernel_end = info->ktext + info->kernel_total;
	kernel_end = (kernel_end + QIZO_PAGE_SIZE - 1) & ~(QIZO_PAGE_SIZE - 1);
	if (info)
		mark_range(info->ktext, kernel_end - info->ktext, 0);
	else
		mark_range((u64)__qizo_phys_base, (u64)__qizo_image_end - (u64)__qizo_phys_base, 0);
	if (info) {
		mark_range(info->blob_phys, info->blob_size, 0);
		if (info->scratch + 0x80000 <= top)
			mark_range(info->scratch, 0x80000, 0);
	}
	free_frames = 0;
	for (i = 0; (u64)i < total_frames; i++) {
		if (frame_ok(i))
			free_frames++;
	}
	used_frames = 0;
	hint = 0;
}

u64 qizo_pma_alloc(void)
{
	u64 start = hint;
	u64 i;

	for (i = 0; i < total_frames; i++) {
		u64 frame = start + i;

		if (frame >= total_frames)
			frame -= total_frames;
		if (!frame_ok(frame))
			continue;
		mark(frame, 0);
		if (free_frames)
			free_frames--;
		used_frames++;
		hint = frame + 1;
		if (hint >= total_frames)
			hint = 0;
		return phys_to_virt(frame * QIZO_PAGE_SIZE);
	}
	return 0;
}

void qizo_pma_free(u64 addr)
{
	u64 frame;

	if (addr < QIZO_HHDM_VIRT)
		return;
	frame = (addr - QIZO_HHDM_VIRT) / QIZO_PAGE_SIZE;
	if (!frame_ok(frame))
		return;
	mark(frame, 1);
	if (free_frames < total_frames)
		free_frames++;
	if (used_frames)
		used_frames--;
}

u64 qizo_pma_total(void)
{
	return total_frames;
}

u64 qizo_pma_free_count(void)
{
	return free_frames;
}

u64 qizo_pma_reserved(void)
{
	return used_frames;
}

u64 qizo_pma_max_phys(void)
{
	return max_phys;
}

void *qizo_page_alloc(size_t pages)
{
	u64 first = 0;
	u64 run = 0;
	u64 i, j;

	if (!pages)
		pages = 1;
	for (i = 0; i < total_frames; i++) {
		if (frame_ok(i)) {
			if (!run)
				first = i;
			run++;
			if (run == pages)
				break;
		} else {
			run = 0;
		}
	}
	if (run < pages)
		return 0;
	for (j = 0; j < pages; j++)
		mark(first + j, 0);
	free_frames -= pages;
	used_frames += pages;
	return (void *)phys_to_virt(first * QIZO_PAGE_SIZE);
}

void qizo_page_free(void *addr, size_t pages)
{
	u64 frame, j;

	if (!addr)
		return;
	frame = ((u64)addr - QIZO_HHDM_VIRT) / QIZO_PAGE_SIZE;
	for (j = 0; j < pages; j++)
		mark(frame + j, 1);
	free_frames += pages;
	used_frames -= pages;
}
