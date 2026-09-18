# Changelog

## v0.1.0

First bootable release. Everything here is new.

### boot chain

- `boot/stage1.S`: 448 byte MBR program, INT 13h AH=42h reads in 64 sector chunks,
  teletype error reporting, argument block at 0x1D0 patched by the image tool.
- `boot/stage2.S`: A20 gate enable and verify, CPUID and long mode probe, E820 memory
  map collection, adler32 integrity check, LZSS decoder in 16 and 32 bit code, module
  header validation, 2 MiB page identity map, protected mode and long mode handoff.
- `kernel/boot64.S`: 1 GiB identity map plus 1 GiB higher half direct map, CR0 and CR4
  programming for SSE, EFER with SCE, LME and NXE, boot stack and page tables in `.bss`.
- `boot/qizoboot.inc` is the only place addresses are written down. The kernel header and
  the tools read it, and `_Static_assert` keeps the C mirror honest.

### kernel

- COM1 and VGA text console, printf, panic path.
- Generated 5x7 column font turned into a 2 VGA font, exported as PSF1 and a C array.
- CPUID vendor, brand, family, model and feature bits including AVX2, AVX512F, ERMS,
  1 GiB pages, SMEP and SMAP.
- Static bitmap physical frame allocator over the E820 map with a page level API.
- 256 entry IDT with generated stubs, remapped 8259, PIT at 100 Hz, PS/2 set 2
  keyboard with a scan code queue.
- Hand written SSE2 copy and fill, ERMS friendly fallbacks.
- Shell with help, info, cpu, mem, bench, micro, ticks, speed, about, clear, reboot.
- `bench` reports copy and fill throughput and picks between SSE2 and `rep movsb`.

### tooling and CI

- `tools/mkqizoimg`: hybrid MBR disk with the loader region, the compressed module blob and
  a FAT16 volume that carries `README.TXT` and `KERNEL.BIN`.
- `tools/mkqizoiso`: El Torito image embedding the exact disk, with a validated boot
  catalog and root directory records.
- `tools/qizocheck`: validates the stage1 arguments, the blob checksum, the module header,
  the FAT directory tree and the ISO descriptors, then replays the whole boot chain in
  Python and fails on any memory region overlap.
- `tools/qizocheck/qizotest.py`: codec round trips, checksum equivalence, layout
  invariants and font sanity.
- `tools/ci/qizosize.py`: size budgets for every artifact, the kernel image and both boot
  stages.
- GitHub Actions: build, test, validate, QEMU boot smoke, release on tag.

### sizes

| artifact | size |
|---|---|
| kernel flat image | 33 KiB of a 128 KiB budget |
| module blob after LZSS | 17 KiB, 51 percent of the image |
| qizo.img | 2.2 MiB |
| qizo.img.xz | 26 KiB |
| qizo.iso | 2.2 MiB |
| qizo.iso.xz | 26 KiB |
