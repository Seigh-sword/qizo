#include "qizo.h"

#define PCI_CFG_ADDR 0xCF8
#define PCI_CFG_DATA 0xCF4
#define PCI_MAX_SLOTS 32
#define PCI_MAX_DEVS 32
#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139
#define RTL_CMD 0x30
#define RTL_CR 0x37
#define RTL_CONFIG0 0x38
#define RTL_RESET 0x10
#define RTL_SPIN 200000

struct pci_slot {
	u32 dev;
	u32 func;
	u32 vend;
	u32 devid;
	u32 class;
	u32 bar0;
};

static struct pci_slot slots[PCI_MAX_SLOTS];
static u32 nslots;
static int rtl_slot = -1;
static u32 rtl_io;
static u8 rtl_mac[6];
static u32 rtl_cr;
static u32 rtl_config0;
static int eeprom_done;

static u32 pci_in(u32 reg)
{
	u32 v;

	__asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(reg));
	return v;
}

static void pci_out(u32 reg, u32 value)
{
	__asm__ volatile("outl %0, %1" :: "a"(value), "Nd"(reg));
}

static u32 cfg_read(u32 dev, u32 func, u32 reg)
{
	pci_out(PCI_CFG_ADDR, 0x80000000U | (dev << 11) | (func << 8) | (reg & 0xFC));
	return pci_in(PCI_CFG_DATA);
}

static void cfg_write(u32 dev, u32 func, u32 reg, u32 value)
{
	pci_out(PCI_CFG_ADDR, 0x80000000U | (dev << 11) | (func << 8) | (reg & 0xFC));
	pci_out(PCI_CFG_DATA, value);
}

static u8 io_in8(u32 port)
{
	return qizo_inb((u16)port);
}

static void io_out8(u32 port, u8 value)
{
	qizo_outb((u16)port, value);
}

static void pci_scan(void)
{
	u32 dev;

	nslots = 0;
	for (dev = 0; dev < PCI_MAX_DEVS; dev++) {
		u32 id = cfg_read(dev, 0, 0);

		if ((id & 0xFFFF) == 0xFFFF || id == 0)
			continue;
		if (nslots == PCI_MAX_SLOTS)
			return;
		slots[nslots].dev = dev;
		slots[nslots].func = 0;
		slots[nslots].vend = id & 0xFFFF;
		slots[nslots].devid = id >> 16;
		slots[nslots].class = cfg_read(dev, 0, 0x08) >> 8;
		slots[nslots].bar0 = cfg_read(dev, 0, 0x10) & ~3U;
		nslots++;
	}
}

static int find_rtl(void)
{
	u32 i;

	for (i = 0; i < nslots; i++) {
		if (slots[i].vend == RTL_VENDOR && slots[i].devid == RTL_DEVICE) {
			rtl_slot = (int)i;
			rtl_io = slots[i].bar0;
			if (rtl_io == 0)
				rtl_io = cfg_read(slots[i].dev, 0, 0x14) & ~3U;
			return 1;
		}
	}
	return 0;
}

int qizo_pci_probe(void)
{
	pci_scan();
	return nslots != 0;
}

void qizo_pci_start(void)
{
}

const char *qizo_pci_detail(void)
{
	return nslots ? "pci cfg space, bus 0" : "no pci";
}

u32 qizo_pci_count(void)
{
	qizo_drivers_start();
	return nslots;
}

int qizo_pci_slot_info(u32 i, u32 *vend, u32 *devid, u32 *class)
{
	if (i >= nslots)
		return 0;
	*vend = slots[i].vend;
	*devid = slots[i].devid;
	*class = slots[i].class;
	return 1;
}

u64 qizo_pci_bar0(u32 i)
{
	if (i >= nslots)
		return 0;
	return slots[i].bar0;
}

int qizo_rtl_probe(void)
{
	if (!nslots)
		pci_scan();
	return find_rtl();
}

void qizo_rtl_start(void)
{
	u32 cmd;
	u32 i;

	if (rtl_slot < 0)
		return;
	cmd = cfg_read(slots[rtl_slot].dev, 0, 0x04);
	cfg_write(slots[rtl_slot].dev, 0, 0x04, cmd | 0x05);
	if (!rtl_io)
		return;
	io_out8(rtl_io + RTL_CMD, RTL_RESET);
	for (i = 0; i < RTL_SPIN; i++) {
		if (!(io_in8(rtl_io + RTL_CMD) & RTL_RESET))
			break;
	}
	io_out8(rtl_io + RTL_CR, 0x00);
	for (i = 0; i < 6; i++)
		rtl_mac[i] = io_in8(rtl_io + (u32)i);
	rtl_config0 = io_in8(rtl_io + RTL_CONFIG0);
	rtl_cr = io_in8(rtl_io + RTL_CR);
	eeprom_done = (rtl_config0 & 0x01) != 0;
}

const char *qizo_rtl_detail(void)
{
	if (rtl_slot < 0)
		return "no rtl8139";
	if (!rtl_io || !eeprom_done)
		return "rtl8139 present, idle";
	return "rtl8139 io, rx idle";
}

int qizo_net_present(void)
{
	qizo_drivers_start();
	return rtl_slot >= 0 && eeprom_done;
}

void qizo_net_mac(u8 *out)
{
	u32 i;

	for (i = 0; i < 6; i++)
		out[i] = rtl_mac[i];
}

u32 qizo_net_io(void)
{
	return rtl_io;
}

int qizo_net_io_port(u32 *out)
{
	if (rtl_slot < 0 || !rtl_io)
		return 0;
	*out = rtl_io;
	return 1;
}
