# Roadmap

## v0.1.0 (this release)

- two stage boot loader written here, no third party boot software
- INT 13h LBA reads with chunking, A20 enabling, E820 collection
- adler32 integrity check on the module blob
- LZSS container, in-place decoder in 16 and 32 bit code
- 2 MiB page identity map, long mode handoff with a fixed bootinfo block
- COM1 plus VGA text console, custom 5x7 derived font and PSF1 export
- CPUID feature detection including AVX-512 and ERMS reporting
- static bitmap physical frame allocator, page level API
- IDT with 256 stubs, PIC remap, PIT tick, PS/2 set 2 keyboard
- shell with info, cpu, mem, bench, micro, ticks, speed, about, clear, reboot
- hybrid disk image, El Torito ISO, xz compressed artifacts, CI build plus QEMU smoke

## v0.2

- ELF module loader so userspace binaries can be shipped inside the FAT volume
- proper kernel heap on top of the frame allocator, no fixed pools
- read-write FAT16 in the kernel so `save` and logs survive a reboot
- serial console becomes the primary sink, VGA becomes optional
- preemptive round robin scheduler on the PIT tick, `ps` and `kill` in the shell
- `bench` gains disk read throughput against the raw partition

## v0.3

- framebuffers: VBE and boot services handoff tables, LFB text terminal with the generated font
- userspace: a tiny ELF entry, syscall gate through `syscall`, `fork` and `execve` subset
- copy on write fork, per process address spaces on 2 MiB pages with 4 KiB tail pages
- ext2 read only driver so a real Linux toolchain can drop binaries onto the image
- `perf` command reading the raw PMC counters over the shared benchmark harness

## v0.4 and beyond

- SMP: INIT INIT SI wakeup, per CPU stacks and idle loops, spin locks sized to cache lines
- UEFI stub so the same binary boots without BIOS services
- disk writes with a small journal to stay safe on cheap flash
- network: virtio-net plus a minimal TCP stack, `netinfo` and `ping`
- power: `cpuidle` with the `monitor`/`mwait` or `hlt` loop, so a laptop does not bake
- a real libc, statically linked, so the userspace programs stop sharing kernel headers

## Explicit non-goals

Anything that does not pay for itself stays out. That includes: loadable kernel modules, a
device tree abstraction, POSIX compliance for its own sake, a graphical environment,
interpreters, and filesystems beyond the ones that earn their code size.
