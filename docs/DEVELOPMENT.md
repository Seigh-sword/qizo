# Development

## Toolchain

Required: gcc, binutils, make, python3 (stdlib only), xz, git. Optional for the smoke test:
qemu-system-x86_64.

`bash tools/ci/bootstrap.sh` verifies all of that and creates `.venv`. The venv holds no
third party packages on purpose: the tools are stdlib only, so a build host can never be
blocked by a package index.

## Makefile targets

| target | what it does |
|---|---|
| `all` | stage1, stage2, kernel, `qizo.img`, `qizo.iso`, both `.xz` |
| `img`, `iso`, `xz` | build one artifact |
| `test` | codec, layout and font unit tests |
| `check` | validate the image, the filesystem, the ISO, then replay the boot chain |
| `size` | bloat budget report, fails when a budget is exceeded |
| `fonts` | regenerate `qizo_font.h` and `qizo.psf1` |
| `dist` | copy the kernel ELF and write `SHA256SUMS` |
| `clean` | remove `build/` |

`make ARCH=native` compiles the kernel for the host CPU. `PYTHON=... make ...` points the
build at another interpreter, which is how CI runs it inside the venv.

## Generated files

`build/` is entirely disposable and ignored:

- `build/include/qizo_font.h` from `tools/common/qizofont.py`
- `build/include/qizo_bootinfo.h` from `boot/qizoboot.inc`

Everything that includes them must be rebuilt after touching the inputs; make handles it
through the order-only prerequisites in the Makefile.

## Where things live

| path | contents |
|---|---|
| `boot/qizoboot.inc` | the memory map, single source of truth |
| `boot/stage1.S` | MBR loader, 448 usable bytes plus the patched argument block |
| `boot/stage2.S` | A20, E820, adler32, LZSS decoder, page tables, long mode jump |
| `boot/*.ld` | link scripts that place the stages at their physical addresses |
| `kernel/boot64.S` | paging handoff, CPUID and MSR helpers, boot stack, page tables |
| `kernel/idt.S` | 256 stubs, save and restore, EOI |
| `kernel/lib/sse2.S` | SSE2 copy and fill, `rep movsb` fallback |
| `kernel/console.c` | COM1 and VGA text console, printf |
| `kernel/pma.c` | static bitmap frame allocator |
| `kernel/cpu.c` | brand, vendor, feature bits |
| `kernel/kbd.c` | PS/2 set 2 translation, scancode queue |
| `kernel/shell.c` | line editor and commands |
| `kernel/bench.c` | memory bandwidth and micro benchmarks |
| `tools/mkqizoimg` | builds the hybrid disk image |
| `tools/mkqizoiso` | wraps the disk into an El Torito ISO |
| `tools/qizocheck` | artifact validation and the Python boot model |
| `tools/qizoartifacts` | font and header generators |
| `tools/ci` | bootstrap, build, size budget and hygiene scripts |

## Adding code

- No comments and no emoji in sources. Names carry the meaning, and the CI hygiene check
  rejects both.
- No malloc in the kernel: grab pages with `qizo_page_alloc`, release with `qizo_page_free`.
- No new third party build tool. If something is needed at build time, write it in
  `tools/` with stdlib python and call it from the Makefile.
- Anything that changes the handoff must update `boot/qizoboot.inc` only; the C header and the
  tools follow it. `_Static_assert` lines catch mistakes at compile time.
- New boot regions go into `tools/qizocheck/qizobootmodel.py` so the overlap check covers them.
- Keep the sizes honest: `make size` fails if the kernel image exceeds its 128 KiB budget or
  an artifact passes its download budget.

## Testing locally

```sh
make test check size
qemu-system-x86_64 -m 64 -drive file=build/artifacts/qizo.img,format=raw,if=ide \
    -display none -serial stdio -monitor none
```

Type `info`, `bench`, `mem`, then `reboot`. QEMU needs no flags beyond the disk; the image is
a real MBR disk and boots from a USB stick or a hard drive the same way.

## CI

`.github/workflows/build.yml` runs on every push and pull request: venv and
toolchain check, `tools/ci/build.sh`, `qizocheck` plus the boot model, hygiene, the
size budgets, and it uploads `qizo.img`, `qizo.iso`, both `.xz` blobs, the PSF1 font
and `SHA256SUMS`. A second job installs QEMU and boots both artifacts, waiting for
the `qizo> ` prompt; that job may show a red cross without failing the run, because
a runner with no QEMU or no working nested virtualisation should be reported, not
hidden.

`.github/workflows/artifacts.yml` is the download path. It builds, boots both
images, then `tools/ci/publish.sh` produces versioned assets in `dist/`
(`qizo-<version>.img`, `.iso`, `.img.xz`, `.iso.xz`, `.img.gz`, the kernel ELF, the
font, the boot logs, `SHA256SUMS`), re-verifies every checksum, uploads them as
workflow artifacts and attaches them to a rolling `latest` prerelease.
`release.yml` does the same for a `v*` tag as a proper release.

Both use `tools/ci/qemuboot.sh`, which is also what you run locally:

```sh
bash tools/ci/qemuboot.sh build/artifacts/qizo.img disk
bash tools/ci/qemuboot.sh build/artifacts/qizo.iso iso
```

It exits 0 when the shell prompt appeared, 1 when it did not, and 2 when QEMU is not
available, which the workflows read as a skip. It writes `serial-<tag>.log`,
`qemu-<tag>.err` and `.qizo-boot-<tag>.status`; the annotation lines it prints are
what shows up in the Actions log when a boot goes wrong. Set `QIZO_BOOT_EXPECT` to
wait for a different string, `QIZO_BOOT_TIMEOUT` for a different deadline.

## Running the boot code itself on the host

`make check` runs `tools/qizoasm/qizoasmrun.py`, which takes the real
`qizo_decompress` bytes out of `boot/stage2.S`, wraps them in a 32 bit program
that decompresses the payload actually sitting in `build/artifacts/qizo.img`,
and compares the result with `tools/common/qizolzss.py`. It is not a model of
the boot code: the assembler output is executed on the host CPU, so a decoder
bug cannot hide behind a reimplementation. Hosts that cannot build or run
32 bit code report a skip instead of a failure.

Run it on its own while working on the boot decompressor:

```sh
make all
python3 tools/qizoasm/qizoasmrun.py --image build/artifacts/qizo.img
```

The same trick works for any leaf routine in `boot/stage2.S`: extract the
labels, hand it its inputs in `.data`, and compare against the python tool
that produced the bytes.
