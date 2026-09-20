# Boot contract

Everything below is fixed by `boot/qizoboot.inc`. The same file generates
`build/include/qizo_bootinfo.h`, so the assembly and the C struct cannot drift.

## Disk layout (LBA, 512 byte sectors)

| range | contents |
|---|---|
| 0 | stage1 (MBR). Code ends by LBA0+464, args at 0x1D0, signature at 0x1FE |
| 1..15 | MBR partition table region (partition 1 starts at LBA 58) |
| 16..23 | stage2, loaded at 0x20000, exactly 4 KiB of room |
| 24..24+n | module blob: LZSS container, loaded at 0x21000 |
| 58+ | FAT16 volume, `README.TXT` and `KERNEL.BIN` for inspection only |

The blob region is deliberately *before* the partition, so the filesystem never has to
resist the loader's own bytes, and the loader never touches a filesystem.

## stage1

Boot sector loaded at 0x7C00. It saves the drive number, then reads `total` sectors from
LBA `lba` into 0x20000 using INT 13h AH=42h in chunks of at most 64 sectors, advancing the
destination segment by 2048 paragraphs per chunk. On read failure it prints
`QIZO STAGE1 DISK ERR` through INT 10h teletype and halts. Then `ljmp $0x2000, $0`.

The four patched words in the MBR (written by `tools/mkqizoimg`):

| offset | field |
|---|---|
| 0x1D0 | drive byte (INT 13h unit) |
| 0x1D4 | u32 lba of the span to read (16) |
| 0x1D8 | u16 sector count (8 + blob sectors) |
| 0x1DA | 2 spare |
| 0x1DC | u32 adler32 of the blob bytes |
| 0x1E0 | u8 error code, written by stage1 on disk failure |

## stage2

Runs at 0x20000 in real mode first:

1. writes the bootinfo block at 0x10000,
2. tests A20, enables it through port 0x92 if the gate is closed, fails the boot otherwise,
3. checks CPUID availability and long mode (`0x80000001:EDX[29]`),
4. collects up to 32 E820 entries into 0x41000 with `INT 15h E820`, failing if the BIOS
   returns nothing,
5. loads the GDT (flat 32-bit code/data, flat 64-bit code/data, user segments), sets CR0.PE,
   and far jumps into 32-bit mode.

In 32-bit mode:

1. copies the E820 entries into bootinfo and totals usable RAM,
2. adler32 over the blob; mismatch aborts the boot,
3. LZSS decompresses the blob at 0x180000, or copies it verbatim when the container is raw,
4. validates the 64 byte module header,
5. copies `kernel_size` bytes to 0x100000 and zeroes `.bss`,
6. builds 4GiB of 2 MiB identity mappings at 0x300000, enables PAE + NXE + LME, sets CR0.PG,
7. far jumps into 64-bit and jumps to `0x100000 + kernel_entry` with the bootinfo address in RDI.

Any failure lands back in real mode printing `QIZO BOOT ERR` on the console.

## Module blob

```
u32 magic        'QIZL' 0x51495A4C for LZSS, 0 for a raw stream
u32 total_out    decompressed length in bytes
...              stream
```

The stream decompresses to:

```
struct qizo_module {         64 bytes
    u32 magic;               'IZOK' 0x4B4F5A49
    u32 kernel_size;         loaded bytes, image without .bss
    u32 kernel_entry;        offset from the load address
    u32 kernel_total;        kernel_size plus .bss
    u32 ktext;               must equal 0x100000
    u32 sector_hint;
}
u8 image[kernel_size];
```

LZSS format: blocks of one flag byte followed by eight items. Flag bit set means a 16-bit
match `((offset-1) << 4) | (length-3)`, with length 3..18 and a backward window of 4 KiB;
bit clear means a literal byte.

## bootinfo (0x10000, 0x700 bytes)

| offset | field |
|---|---|
| 0x00 | u32 magic 0x00010001 |
| 0x04 | u8 drive, 0x05 mode (1 = BIOS), 0x06 err, 0x07 pad |
| 0x08 | u32 blob lba |
| 0x0C | u32 blob sectors |
| 0x10 | u32 blob adler32 |
| 0x14 | u32 blob phys |
| 0x18 | u32 blob size |
| 0x1C | u32 scratch |
| 0x30 | u32 ktext, 0x34 kernel_size, 0x38 kernel_entry, 0x3C kernel_total, 0x40 kernel_bss |
| 0x44 | u32 boot version |
| 0x50 | u32 cpus, 0x54 cpuid max leaf, 0x58 long mode, 0x59 a20 |
| 0x100 | E820 entries, 24 bytes each: u64 base, u64 length, u32 type, u32 acpi |
| 0x400 | u32 entry count |
| 0x600 | 16 byte vendor, 0x610 80 byte brand |
| 0x670 | u64 total usable RAM |

## Fixed physical addresses

| address | use |
|---|---|
| 0x7C00..0x7DFF | stage1 and its 512 byte sector |
| 0x7E00 | INT 13h AH=42h disk address packet, paragraph aligned |
| 0x10000..0x106FF | bootinfo |
| 0x20000..0x20FFF | stage2 code |
| 0x21000.. | blob, up to 124 KiB |
| 0x41000 | E820 scratch, 0x41300 count |
| 0x44000 | stage2 protected mode stack top |
| 0x100000 | kernel load address (identity mapped, 144 KiB budget) |
| 0x180000 | decompression scratch |
| 0x300000 | stage2 page tables: PML4, PDPT, then 4 PDT pages (6 pages, 0x6000) |

Nothing may be loaded between 0xA0000 and 0xFFFFF. 0xA0000..0xBFFFF is the planar VGA
frame buffer and 0xC0000..0xEFFFF is the video BIOS and option ROM shadow, so code placed
there is written to the graphics card and read back as display memory, and the region above
0x9FC00 belongs to the extended BIOS data area. The load area therefore sits at 0x20000,
inside ordinary conventional RAM, clear of the IVT, the BIOS data area, the MBR and the
EBDA, and it still leaves the whole first megabyte reserved by the frame allocator.

### The disk address packet

stage1 reads the boot area with INT 13h AH=42h. The packet layout is what the firmware
struct says, not what a comment claims, and SeaBIOS reads it as

    struct int13ext_s {
        u8  size;              // +0, must be 0x10
        u8  reserved;          // +1, must be 0
        u16 count;             // +2, sectors to transfer
        struct segoff_s data;  // +4 buffer offset, +6 buffer segment
        u64 lba;               // +8 starting sector
    };

so the buffer address is at +4 and +6 and the block number starts at +8.

Where the packet *itself* is pointed at is the part no summary of the spec gets right, and
it is the reason a boot can print 0x01 forever: the INT 13h AH=42h documentation says the
firmware takes the packet address in ES:BX, and SeaBIOS reads it from **DS:SI** instead,
dereferencing `(struct int13ext_s *)(regs->si+0)` with `GET_FARVAR(regs->ds, ...)`. Real
firmware does one thing, the emulator does the other, and only a loader that sets both
boots on both. stage1 puts the same paragraph aligned address in BX and SI before every
call, and `make check` requires both stores in the disassembly. Putting the
segment at +8 instead reads the transfer segment as the LBA, which for a load at 0x20000
means asking for sector 8192 of a 4479 sector image, and the firmware answers with
0x01 DISK_RET_EPARAM. The packet also has to sit on a 16 byte paragraph, which is why it
lives at 0x7E00 right after the MBR copy rather than in the tail of the sector, where the
next free offset is four bytes past a paragraph.

Before the first read stage1 asks AH=41h with BX=0x55AA which drive actually supports
extended reads, starting from the drive the BIOS entered the MBR with and falling back to
0x80, 0xE8, 0xF0 and 0xF8, because QEMU's firmware hands an El Torito boot the CD-ROM
number rather than a hard disk number, and a read of a drive the BIOS does not know is
answered with the same 0x01. The number it settled on is what stage2 and the kernel boot
info are told to use, and the serial marker prints the probe answer and the chosen drive.

`make check` disassembles stage1 and requires a store at +0, +2, +4, +6, +8 and +12, and
fails on a store at +10, so the field order is a tested contract and not a memory.

`tools/qizocheck/qizobootmodel.py` re-runs the whole chain in Python against the built image
and fails if any region overlaps, so these numbers stay honest. It cannot catch a load
address that is only *shadowed*, so `tools/ci/qemuboot.sh` boots the image under qemu and
waits for the `qizo> ` prompt, which is the test that found this one.

## Boot markers

Both loader stages drive COM1 at 115200 8N1 themselves and write one marker per step, so a
boot that dies still says where. `tools/ci/qemuboot.sh` reads the bytes back and its
annotation lists the markers it found plus the first bytes of the log as hex, because a
message built out of guest bytes is not something the runner promises to deliver.

| marker | meaning |
|---|---|
| `1:` | stage one is running, COM1 initialised |
| `X<ah><cl><drive>` | the AH=41h answer, the support bitmap, and the drive chosen for reads |
| `R` | one INT 13h chunk read without error |
| `J` | the whole boot area is in, jumping to stage two |
| `E<err><drive>` | stage one failed: the firmware code, then the drive number it used |
| `2:` | stage two is running, COM1 initialised |
| `A1` | the A20 gate was tested for aliasing and is open |
| `A0` | the gate would not open; the boot continues and records it in bootinfo |
| `a` | the descriptor table pointer is loaded |
| `P` | protection enable is committed, the cpu is in protected mode |
| `m` | a far jump loaded a descriptor and its first instruction ran |
| `M` | in protected mode, copying the E820 map |
| `L` | page tables built, about to enter long mode |
| `6` | in long mode, jumping to the kernel |
| `E` + two hex digits | stage two failed, the number is the `err` field of bootinfo |

So `1:X300780RJ2:A1PML6` means the bootloader did its whole job and the kernel took over,
and after it the kernel prints `console up`, `memory up`, `irqs live`, `calibrating`,
`timer`, `reporting`, `shell` and then `qizo> `. A bare `1:` means the port works and
nothing else does. `E01` from stage one is the firmware refusing the read, which is what a
wrong packet, a wrong field offset, or a drive number the firmware does not watch all look
like, so the marker carries the drive it used. `E11` is A20, `E12` is no long mode, `E13`
is a CPU that cannot flip the interrupt flag, `E14` is no memory map, `E16` is a bad blob,
header or decompression. Set `QIZO_BOOT_TRACE` to `0` in `boot/qizoboot.inc` to drop the
markers and reclaim about 200 bytes of stage two.
### How to read a boot that stops

A boot that dies still says where it died. Four things about these instructions are worth
knowing before changing any of them, because each looks correct in the source and wrong to
the cpu, and each one cost a real boot attempt.

- A descriptor is bytes in a fixed order, and it is not the order the words suggest: limit
  0:15 at byte 0, base 0:15 at byte 2, **base 16:23 at byte 4**, access at byte 5, flags
  with limit 16:19 at byte 6, base 24:31 at byte 7. Byte 4 belongs to the base, not to the
  attributes. `ff ff 00 00 9a cf 00 00` therefore puts the access byte where base bits
  16:23 live, which the cpu reads as a present segment of type 0xf at base 0x9a0000, and
  `long 0xFFFFFFFF, 0x00CF9A00` puts a base of 0xffff in the low word for the same reason.
  Both are one byte away from correct, which is far enough for the jump into protected mode
  to fault with vector 13. `make check` decodes the table with the bit positions the cpu
  uses and refuses the build when a selector does not describe what this file claims it
  does, so the check is the thing that has to be edited when a layout deliberately changes.
- `lgdt` in 16 bit mode takes a six byte pseudo descriptor, limit then a 32 bit base. The
  `l` suffix makes the assembler add a 66 operand size override, and an overridden `lgdt`
  reads a *ten* byte form with a 64 bit base, so the base runs two bytes past the field
  into the code that follows and the installed table pointer is garbage. The bare `lgdt` is
  the right mnemonic here. In 64 bit mode the ten byte form is the only one, which is why
  the kernel writes the same looking instruction with a quad sized pointer on purpose.
- A trace marker is a call, and calls clobber registers. The real mode helper takes its
  character in `%al`, so a marker placed between the `or` that sets protection enable and
  the store that commits it clears bit 0 of the value being written: the boot then prints a
  success marker for a mode switch that never happened and every conclusion drawn from it
  is wrong. Markers go after the instruction they describe, never inside a register
  handoff, and stage2 saves `%ax` across the one that follows the cr0 store.
- A new mode needs its stack before its first push. Entering protected mode does not change
  `SS`, and the value stage one uses throughout is the null descriptor there, so a `pushal`
  as the first protected mode instruction faults before anything can report it, and with no
  idt loaded the fault on the fault is a reset that repeats as fast as the emulator can
  spin. Each mode in stage2 loads its segments and stack pointer first, then writes its
  marker straight to the port with no call and no stack, because a 16 bit routine reached
  from 32 bit code executes as 32 bit instructions.

The sequence all of this supports is: load the descriptor table, set `cr4`, commit protection
enable, far jump to the 16 bit code descriptor so the first fetch in the new mode comes from a
descriptor the gdt supplied, load the data segments and the protected mode stack, then far jump
to the 32 bit code descriptor. One jump changes one thing.

### Stage2 fail codes

The byte after `E` on the serial port is `QIZO_BOOTINFO + QIZO_BI_ERR`.

| code | meaning |
| --- | --- |
| 0x11 | A20 line still aliased, so the high 64 KiB is not reachable |
| 0x12 | no long mode (CPUID extended feature bit clear) |
| 0x13 | EFLAGS.IF still set after `cli` |
| 0x14 | no usable memory map (BIOS E820 failed or reported no RAM) |
| 0x15 | malformed LZSS stream (input exhausted before the output was complete) |
| 0x17 | adler of the loaded blob does not match the value stage1 carried |
| 0x18 | decompressor bailed |
| 0x19 | module magic after decompression |
| 0x1a | kernel text size larger than `QIZO_KERNEL_MAX` |
| 0x1b | kernel image size larger than `QIZO_KERNEL_MAX` |
| 0x1c | kernel image size smaller than its own text size |
| 0x1d | module magic wrong at the copy destination |

Every one of these halts with `E` and the code, so a halted boot is a
statement about the payload, not a silent crash.

## CPU exception report

Every vector below 32 is a CPU exception and the kernel does not return from one:
returning would run the faulting instruction again, fault again, and live-lock
with nothing on the serial line but the last stage marker. Instead it prints

    qizo: cpu exception <vector> rip=<addr> code=<err> cr2=<addr>

and halts. `cr2` is the faulting address for a page fault (14) and zero for the
rest. The vectors are the standard ones: 0 #de divide, 1 #db debug, 3 #bp
breakpoint, 4 #of overflow, 5 #br bound range, 6 #ud invalid opcode, 7 #nm device
not available, 8 #df double fault, 10 #ts invalid TSS, 11 #np segment not present,
12 #ss stack segment, 13 #gp general protection, 14 #pf page fault, 16 #mf x87
floating point, 17 #ac alignment check, 19 #simd SIMD floating point. The kernel
is identity mapped from 0x100000, so `rip` names the function on its own:

    nm build/qizo.elf | sort > /tmp/syms

and the largest symbol at or below `rip` is where it faulted. No symbol table
needs to live in the image.

## Faults before the kernel can report them

While stage2 hands over, no interrupt descriptor table exists yet, so any
exception would be a triple fault and the machine would just restart. stage2
therefore builds a 32 entry idt at `0x307000` for the CPU exceptions and points
every entry at a stub that prints one line on COM1 and halts, 16 upper case hex
digits per field:

```
F<vector><error code><faulting rip><cr2>
```

Every stub pushes a zero error code first for the exceptions that do not supply
one, so the frame layout is the same for all 32. The gates load selector
`0x28`, the 64 bit code segment of the boot gdt: `0x08` is its 32 bit segment,
and an interrupt frame that enters it does not run the handler at all.

The kernel installs its own table a few instructions later, so this costs one
page of memory and only lives during the handoff. It is assembled
unconditionally: a boot that can describe its own failure is worth more than
the bytes it takes. `make check` runs the handler and the gate builder on the
host, with the privileged instructions replaced by memory writes, so the
reporter itself is tested and not just trusted.

## Kernel entry markers

With a debug build the kernel prints one character to COM1 at each step of its
own entry path, so a fault that happens before the console is up can still be
placed. The marker is emitted after the step it names:

| marker | meaning |
| --- | --- |
| `E` | `qizo_kernel_entry` reached, still running on the boot page tables |
| `g` | GDT loaded and the 64 bit code segment reloaded through it |
| `i` | interrupt descriptor table built and loaded |
| `p` | new page tables live: `cr3`, `cr4`, `efer`, `cr0` rewritten |
| `K` | about to jump into `qizo_kmain` with the boot info pointer in `rdi` |

So a trace that ends in `E` but never prints `g` is a fault inside the GDT
reload, and `Egi` without `p` is a fault in the paging transition.

## What the boot smoke job prints

`tools/ci/qemuboot.sh` asks qemu for `-d int,cpu_reset,guest_errors`. QEMU logs
a cpu state dump with every exception it takes, so the annotation carries the
first `RIP`, `CR0`, `CR2`, `CR3`, `CR4` and `EFER` of the run next to the marker
trace: `RIP` is the faulting instruction and `CR2` the address a page fault
tried to reach. Map `RIP` onto the kernel with

```sh
nm build/qizo.elf | sort > /tmp/syms
```

and look for the largest symbol address below it, or onto the boot blob by
subtracting `0x20000` and dumping `build/boot/stage2.bin` at that offset.
