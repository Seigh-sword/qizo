QIZO_VERSION := $(shell cat VERSION 2>/dev/null || echo 0.1.0)
ARCH ?= generic
CC := gcc
AS := as
LD := ld
OBJCOPY := objcopy
PYTHON := python3
BUILD := build
TOOLS := tools
BOOT := boot
KERNEL := kernel
ART := $(BUILD)/artifacts

ifeq ($(ARCH),native)
QIZO_MARCH := -march=native
else
QIZO_MARCH := -march=x86-64 -mtune=generic
endif

KCFLAGS := -std=gnu11 -O3 $(QIZO_MARCH) -ffreestanding -fno-pic -fno-pie -no-pie \
	-fno-stack-protector -fno-asynchronous-unwind-tables -fno-builtin \
	-fno-tree-loop-distribute-patterns -fomit-frame-pointer -fno-strict-aliasing \
	-fno-common -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-sse3 \
	-Wall -Wextra -Wno-unused-parameter -Wno-address-of-packed-member \
	-I$(KERNEL) -I$(BUILD)/include
KLDFLAGS := -m elf_x86_64 -n -static --build-id=none --no-dynamic-linker \
	-z noexecstack -z max-page-size=0x1000
XZ := xz -9e
HAS_XZ := $(shell command -v xz >/dev/null 2>&1 && echo yes)
XZ_ART := $(if $(HAS_XZ),$(ART)/qizo.img.xz $(ART)/qizo.iso.xz)
XZ_NAMES := $(if $(HAS_XZ),qizo.img.xz qizo.iso.xz,)

KERNEL_C := $(notdir $(wildcard $(KERNEL)/*.c) $(wildcard $(KERNEL)/lib/*.c) $(wildcard $(KERNEL)/drivers/*.c))
KERNEL_OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(basename $(KERNEL_C))))
ASM_OBJS := $(BUILD)/obj/boot64.o $(BUILD)/obj/idt.o $(BUILD)/obj/sse2.o
FONT_HDR := $(BUILD)/include/qizo_font.h
FONT_PSF := $(ART)/qizo.psf1

.PHONY: all img iso xz check dist clean help fonts test size

all: $(ART)/qizo.iso $(XZ_ART)

$(BUILD)/obj $(BUILD)/include $(ART) $(BUILD)/boot:
	@mkdir -p $@

$(BUILD)/boot/stage1.o: $(BOOT)/stage1.S $(BOOT)/qizoboot.inc | $(BUILD)/boot
	$(AS) --32 -I$(BOOT) -o $@ $<

$(BUILD)/boot/stage2.o: $(BOOT)/stage2.S $(BOOT)/qizoboot.inc | $(BUILD)/boot
	$(AS) --32 -I$(BOOT) -o $@ $<

$(BUILD)/boot/stage1.bin: $(BUILD)/boot/stage1.o $(BOOT)/stage1.ld | $(BUILD)/boot
	$(LD) -m elf_i386 -T $(BOOT)/stage1.ld --oformat=binary -o $@ $<

$(BUILD)/boot/stage2.bin: $(BUILD)/boot/stage2.o $(BOOT)/stage2.ld | $(BUILD)/boot
	$(LD) -m elf_i386 -T $(BOOT)/stage2.ld --oformat=binary -o $@ $<

$(BUILD)/include/qizo_bootinfo.h: $(TOOLS)/qizoartifacts/qizobootinfo.py $(BOOT)/qizoboot.inc | $(BUILD)/include
	$(PYTHON) $(TOOLS)/qizoartifacts/qizobootinfo.py $(BOOT)/qizoboot.inc $@

$(FONT_HDR): $(TOOLS)/qizoartifacts/qizofontgen.py $(TOOLS)/common/qizofont.py | $(BUILD)/include
	$(PYTHON) $(TOOLS)/qizoartifacts/qizofontgen.py $@ $(FONT_PSF)

$(BUILD)/obj/%.o: $(KERNEL)/%.c $(FONT_HDR) $(BUILD)/include/qizo_bootinfo.h | $(BUILD)/obj
	$(CC) $(KCFLAGS) -c -o $@ $<

$(BUILD)/obj/%.o: $(KERNEL)/drivers/%.c $(FONT_HDR) $(BUILD)/include/qizo_bootinfo.h | $(BUILD)/obj
	$(CC) $(KCFLAGS) -c -o $@ $<

$(BUILD)/obj/%.o: $(KERNEL)/lib/%.c $(FONT_HDR) $(BUILD)/include/qizo_bootinfo.h | $(BUILD)/obj
	$(CC) $(KCFLAGS) -c -o $@ $<

$(BUILD)/obj/sse2.o: $(KERNEL)/lib/sse2.S | $(BUILD)/obj
	$(AS) --64 -o $@ $<

$(BUILD)/obj/boot64.o: $(KERNEL)/boot64.S | $(BUILD)/obj
	$(AS) --64 -o $@ $<

$(BUILD)/obj/idt.o: $(KERNEL)/idt.S | $(BUILD)/obj
	$(AS) --64 -o $@ $<

$(BUILD)/qizo.elf: $(KERNEL_OBJS) $(ASM_OBJS) $(KERNEL)/kernel.ld | $(BUILD)/obj
	$(LD) $(KLDFLAGS) -T $(KERNEL)/kernel.ld -o $@ $(KERNEL_OBJS) $(ASM_OBJS)

$(BUILD)/qizo.bin: $(BUILD)/qizo.elf
	$(OBJCOPY) -O binary $< $@

$(ART)/qizo.img: $(BUILD)/boot/stage1.bin $(BUILD)/boot/stage2.bin $(BUILD)/qizo.bin $(BUILD)/qizo.elf
	$(PYTHON) $(TOOLS)/mkqizoimg/mkqizoimg.py --stage1 $(BUILD)/boot/stage1.bin \
		--stage2 $(BUILD)/boot/stage2.bin --kernel-bin $(BUILD)/qizo.bin \
		--kernel-elf $(BUILD)/qizo.elf --out $@

$(ART)/qizo.iso: $(ART)/qizo.img
	$(PYTHON) $(TOOLS)/mkqizoiso/mkqizoiso.py --image $< --out $@

$(ART)/qizo.img.xz: $(ART)/qizo.img
	$(XZ) -k -c $< > $@

$(ART)/qizo.iso.xz: $(ART)/qizo.iso
	$(XZ) -k -c $< > $@

test:
	$(PYTHON) $(TOOLS)/qizocheck/qizotest.py

img: $(ART)/qizo.img
iso: $(ART)/qizo.iso
xz: $(XZ_ART)
	$(if $(HAS_XZ),@:,@echo "qizo: no xz on this machine, images are published as they are")
fonts: $(FONT_HDR)

size: $(ART)/qizo.img
	$(PYTHON) $(TOOLS)/ci/qizosize.py

check: $(ART)/qizo.img $(ART)/qizo.iso
	$(PYTHON) $(TOOLS)/qizocheck/qizocheck.py --image $(ART)/qizo.img \
		--kernel-elf $(BUILD)/qizo.elf --stage1 $(BUILD)/boot/stage1.bin \
		--stage2 $(BUILD)/boot/stage2.bin --iso $(ART)/qizo.iso
	$(PYTHON) $(TOOLS)/qizocheck/qizobootmodel.py --image $(ART)/qizo.img \
		--kernel-bin $(BUILD)/qizo.bin
	$(PYTHON) $(TOOLS)/qizoasm/qizoasmrun.py --image $(ART)/qizo.img

dist: all
	cp $(BUILD)/qizo.elf $(ART)/qizo-kernel.elf
	cd $(ART) && sha256sum qizo.img qizo.iso $(XZ_NAMES) qizo-kernel.elf > SHA256SUMS

clean:
	rm -rf $(BUILD)

help:
	@echo "qizo $(QIZO_VERSION)  targets: all img iso xz check dist clean"
	@echo "  make ARCH=native   build tuned for this machine"
