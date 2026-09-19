# Drivers

Everything that touches hardware lives in `kernel/drivers/`. The rest of the
kernel talks to devices through the small surface each driver exports, and the
registry in `driver.c` probes them once, before the shell starts.

| driver | class | hardware | notes |
| --- | --- | --- | --- |
| `kbd` | input | 8042 PS/2 port 1, irq1 | scan code sets 1 and 2, translated by the tables in `kbd.c` |
| `mouse` | input | 8042 PS/2 port 2, irq12 | 3 byte packets, resynchronises on a lost sync bit |
| `ata` | block | primary IDE, 0x1F0 and 0x170 | identify plus 28 bit LBA PIO reads, 16 sectors at a time |
| `fat` | volume | on top of `ata` | reads the boot volume the system loaded from, FAT12 and FAT16 |
| `pci` | bus | config ports 0xCF8 and 0xCF4 | bus 0 scan, keeps vendor, device, class and bar0 |
| `rtl8139` | net | Realtek 8139, found through `pci` | io window, soft reset, mac address; no rx or tx yet |

A driver reports three things: whether the hardware answered (`probe`), what
it had to turn on (`start`), and one line a human can read (`detail`). The
table is printed by `sysmngr -drivers`.

## Adding one

1. Write `kernel/drivers/thing.c` with `qizo_thing_probe`, `qizo_thing_start`
   and `qizo_thing_detail`, and declare them in `kernel/qizo.h`.
2. Add one `slot_add` line in `qizo_drivers_start`. The Makefile picks up any
   `.c` in that directory, so there is nothing else to edit.
3. Keep probe cheap and bounded. Every wait loop has a spin limit, because a
   driver that hangs during boot hangs the machine with no way to report it.

## Network

The plan is one step at a time, in this order, and the step that does not work
stops the ones behind it:

1. `rtl8139`: find the card, own its io window, read the mac address. Done.
2. Frame level: transmit, receive, arp, and `ping` so a reply proves both
   directions.
3. IPv4 plus a client only TCP: three way handshake, one window, a timeout.
4. HTTP/1.0 `GET` and `HEAD` in `kernel/drivers/http.c`, and a shell command
   that reports the status line and the body length.
5. HTTPS only once step 4 works against a real server, and only with a pinned
   certificate chain: a TLS client is a lot of code that must not be wrong.
