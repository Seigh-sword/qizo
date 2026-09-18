# Boot contract

Everything below is fixed by `boot/qizoboot.inc`. The same file generates
`build/include/qizo_bootinfo.h`, so the assembly and the C struct cannot drift.

## Disk layout (LBA, 512 byte sectors)

| range | contents |
|---|---|
| 0 | stage1 (MBR). Code ends by LBA0+464, args at 0x1D0, signature at 0x1FE |
| 1..15 | MBR partition table region (partition 1 starts at LBA 58) |
| 16..23 | stage2, loaded at 0xA0000, exactly 4 KiB of room |
| 24..24+n | module blob: LZSS container, loaded at 0xA1000 |
| 58+ | FAT16 volume, `README.TXT` and `KERNEL.BIN` for inspection only |

The blob region is deliberately *before* the partition, so the filesystem never has to
resist the loader's own bytes, and the loader never touches a filesystem.

## stage1

Boot sector loaded at 0x7C00. It saves the drive number, then reads `total` sectors from
LBA `lba` into 0xA0000 using INT 13h AH=42h in chunks of at most 64 sectors, advancing the
destination segment by 2048 paragraphs per chunk. On read failure it prints
`QIZO STAGE1 DISK ERR` through INT 10h teletype and halts. Then `ljmp $0xA000, $0`.

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

Runs at 0xA0000 in real mode first:

1. writes the bootinfo block at 0x10000,
2. tests A20, enables it through port 0x92 if the gate is closed, fails the boot otherwise,
3. checks CPUID availability and long mode (`0x80000001:EDX[29]`),
4. collects up to 32 E820 entries into 0xD0000 with `INT 15h E820`, failing if the BIOS
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
| 0x10000..0x106FF | bootinfo |
| 0xA0000..0xA0FFF | stage2 code |
| 0xA1000.. | blob, up to 124 KiB |
| 0xD0000 | E820 scratch, 0xD0300 count |
| 0xD3000 | stage2 protected mode stack top |
| 0x100000 | kernel load address (identity mapped, 128 KiB budget) |
| 0x180000 | decompression scratch |
| 0x300000 | stage2 page tables (PML4 + PDPT) |

`tools/qizocheck/qizobootmodel.py` re-runs the whole chain in Python against the built image
and fails if any region overlaps, so these numbers stay honest.

## Boot markers

Stage two drives COM1 at 115200 8N1 itself and writes one marker per stage, before
it trusts anything of its own:

| marker | meaning |
|---|---|
| `2:` | stage two is running, COM1 initialised |
| `P` | about to enter protected mode |
| `M` | in protected mode, copying the E820 map |
| `L` | page tables built, about to enter long mode |
| `6` | in long mode, jumping to the kernel |
| `E` + two hex digits | failed, the number is the `err` field of bootinfo |

So `2:PML6` on the serial line means the bootloader did its whole job and the
kernel took over. Nothing at all means stage one never got the disk read (its own
error is on the screen, not the serial port). `E11` is A20, `E12` is no long mode,
`E14` is no memory map, `E16` is a bad blob, header or decompression. Set
`QIZO_BOOT_TRACE` to `0` in `boot/qizoboot.inc` to drop the markers and reclaim
about 200 bytes of stage two.
