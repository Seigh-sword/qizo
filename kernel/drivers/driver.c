#include "qizo.h"

#define QIZO_MAX_DRIVERS 8

struct qizo_slot {
	const char *name;
	const char *kind;
	int (*probe)(void);
	void (*start)(void);
	const char *(*detail)(void);
	int state;
};

static struct qizo_slot slots[QIZO_MAX_DRIVERS];
static u32 nslots;
static int started;

static void slot_add(const char *name, const char *kind, int (*probe)(void),
		     void (*start)(void), const char *(*detail)(void))
{
	if (nslots >= QIZO_MAX_DRIVERS)
		return;
	slots[nslots].name = name;
	slots[nslots].kind = kind;
	slots[nslots].probe = probe;
	slots[nslots].start = start;
	slots[nslots].detail = detail;
	slots[nslots].state = QIZO_DRIVER_ABSENT;
	nslots++;
}

void qizo_drivers_start(void)
{
	u32 i;

	if (started)
		return;
	started = 1;
	slot_add("kbd", "input", qizo_kbd_probe, 0, qizo_kbd_detail);
	slot_add("mouse", "input", qizo_mouse_probe, qizo_mouse_start, qizo_mouse_detail);
	slot_add("ata", "block", qizo_ata_probe, qizo_ata_start, qizo_ata_detail);
	slot_add("fat", "volume", qizo_fat_probe, qizo_fat_start, qizo_fat_detail);
	slot_add("pci", "bus", qizo_pci_probe, qizo_pci_start, qizo_pci_detail);
	slot_add("rtl8139", "net", qizo_rtl_probe, qizo_rtl_start, qizo_rtl_detail);
	for (i = 0; i < nslots; i++) {
		struct qizo_slot *s = &slots[i];

		if (!s->probe())
			continue;
		s->state = QIZO_DRIVER_PRESENT;
		if (s->start)
			s->start();
		s->state = QIZO_DRIVER_READY;
	}
}

u32 qizo_drivers_count(void)
{
	qizo_drivers_start();
	return nslots;
}

static struct qizo_slot *slot_at(u32 i)
{
	qizo_drivers_start();
	if (i >= nslots)
		return 0;
	return &slots[i];
}

const char *qizo_driver_name(u32 i)
{
	struct qizo_slot *s = slot_at(i);

	return s ? s->name : "";
}

const char *qizo_driver_kind(u32 i)
{
	struct qizo_slot *s = slot_at(i);

	return s ? s->kind : "";
}

const char *qizo_driver_detail(u32 i)
{
	struct qizo_slot *s = slot_at(i);

	if (!s || !s->detail)
		return "";
	return s->detail();
}

int qizo_driver_state(u32 i)
{
	struct qizo_slot *s = slot_at(i);

	return s ? s->state : QIZO_DRIVER_ABSENT;
}

static const char *state_name(int state)
{
	if (state == QIZO_DRIVER_READY)
		return "ready";
	if (state == QIZO_DRIVER_PRESENT)
		return "present";
	return "absent";
}

void qizo_drivers_report(void)
{
	u32 i;
	u32 ready = 0;

	qizo_drivers_start();
	qizo_puts("driver      class    state      detail\r\n");
	for (i = 0; i < nslots; i++) {
		struct qizo_slot *s = &slots[i];

		qizo_puts(s->name);
		qizo_puts(" ");
		qizo_pad(s->name, 12);
		qizo_puts(s->kind);
		qizo_pad(s->kind, 9);
		qizo_puts(state_name(s->state));
		if (s->state == QIZO_DRIVER_READY)
			ready++;
		qizo_puts(" ");
		qizo_pad(state_name(s->state), 11);
		qizo_printf("%s\r\n", s->detail ? s->detail() : "");
	}
	qizo_printf("qizo: %u of %u drivers ready\r\n", ready, nslots);
}
