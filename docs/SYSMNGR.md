# sysmngr

`sysmngr` is the one command that reports the state of the running system. It
reads kernel counters only, so nothing it prints can be faked by a value in a
file, and nothing it does can change the machine.

```
sysmngr -free mem | vram | c/ | d/ | f/ | all
sysmngr -drivers
sysmngr -net
sysmngr -v | -v qizo | -v kernel | -v boot | -v boot stages
sysmngr -update
sysmngr -doc
sysmngr -help | -h
```

## -free

`-free mem` prints frames the page allocator can still hand out, split into
total, free, used and what the kernel holds for itself, plus the memory size
the BIOS reported at boot so the two can be compared.

`-free vram` prints the vga aperture, what the 80x25 colour text screen
occupies, and the remainder. There is no linear framebuffer yet, so the
remainder is not something a program can claim; it is reported honestly as
unusable rather than as free.

`-free c/` reads the volume the system booted from off the disk, through the
`ata` and `fat` drivers, and counts free clusters in the file allocation
table. `d/` and `f/` do the same on the other two ide drives, and say so when
there is no drive there.

## -v

`sysmngr -v` is the version of this command. `-v qizo` is the version of the
whole operating system. `-v kernel` adds how much of the kernel is loaded and
where its entry point sits. `-v boot` reports what the boot loader handed over,
including the fail code it stopped on, and `-v boot stages` lists every stage:
where stage1 and stage2 were loaded, the payload location, its size on disk and
its checksum.

## -update

Not in this build, and it says so. An update needs a block writer, a second
payload slot, and the checksum the boot already verifies, so that a half
written image cannot make the machine unbootable. See `docs/BOOTING.md` for the
checksum path and `docs/ROADMAP.md` for the ordering.

## Compressed images are optional

The build publishes `.xz` images when `xz` is installed and simply skips them
when it is not: `make all`, `make dist`, the CI build and the release publisher
all detect the tool first. Nothing else about qizo depends on it, so a machine
without `xz` still gets the bootable `.img` and `.iso`.
