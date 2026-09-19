import argparse
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "qizoartifacts"))

import qizolzss
import qizolayout as LY
import qizoartifacts as ART

SECTOR = LY.SECTOR
STAGE1_ARGS = LY.STAGE1_ARGS_OFF
SCRATCH = LY.SCRATCH
KTEXT = LY.KTEXT
QIZOK_MAGIC = LY.QIZOK_MAGIC
LZSS_MAGIC = LY.LZSS_MAGIC
BLOB_PHYS = LY.BLOB_PHYS
BLOB_BASE = LY.STAGE2
STAGE2 = LY.STAGE2
STAGE2_BYTES = LY.STAGE2_BYTES
E820_BUF = LY.E820_BUF
PM_STACK = LY.PM_STACK
BOOTINFO = LY.BOOTINFO
BOOTINFO_SIZE = LY.BOOTINFO_SIZE
PAGE_PML4 = LY.PAGE_PML4


def fail(msg):
    print("qizo: check FAIL: " + msg)
    return 1


def check_fs(img, kernel, stage2=None):
    part = struct.unpack_from("<I", img, 0x1C6)[0]
    bs = img[part * SECTOR:part * SECTOR + SECTOR]
    if bs[510:512] != b"\x55\xAA":
        return fail("boot sector signature missing")
    bps = struct.unpack_from("<H", bs, 11)[0]
    spc = bs[13]
    reserved = struct.unpack_from("<H", bs, 14)[0]
    nfats = bs[16]
    rootcnt = struct.unpack_from("<H", bs, 17)[0]
    media = bs[21]
    fat_size = struct.unpack_from("<H", bs, 22)[0]
    if bps != SECTOR:
        return fail("unexpected bytes per sector %d" % bps)
    if media not in (0xF8, 0xF9):
        return fail("unexpected media byte %02x" % media)
    total = struct.unpack_from("<H", bs, 19)[0] or struct.unpack_from("<I", bs, 40)[0]
    root_lba = part + reserved + nfats * fat_size
    data_lba = root_lba + ((rootcnt * 32 + SECTOR - 1) // SECTOR)
    entries = []
    for i in range(rootcnt):
        e = img[(root_lba * SECTOR) + i * 32:(root_lba * SECTOR) + (i + 1) * 32]
        if not e or e[0] == 0:
            break
        if e[11] == 0x0F or e[11] == 0x08:
            continue
        name = e[0:8].decode().strip()
        ext = e[8:11].decode().strip()
        cluster, size = struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
        entries.append((("%s.%s" % (name, ext)).strip("."), size, cluster))
    found = {}
    for name, size, cluster in entries:
        data = bytearray()
        cur = cluster
        spins = 0
        while cur >= 2 and cur < 0xFFF8 and spins < 100000:
            lba = data_lba + (cur - 2) * spc
            data += img[lba * SECTOR:(lba + spc) * SECTOR]
            cur = fat16(img, (part + reserved) * SECTOR, cur)
            spins += 1
        found[name] = bytes(data[:size])
    if "KERNEL.BIN" not in found:
        return fail("KERNEL.BIN missing from root directory")
    if found["KERNEL.BIN"] != kernel:
        return fail("KERNEL.BIN in the filesystem differs from the module kernel")
    if "README.TXT" not in found or not found["README.TXT"].startswith(b"qizo"):
        return fail("README.TXT missing or unexpected")
    print("qizo: fat ok: part lba %d, root %d entries, data lba %d, kernel file %d bytes" %
          (part, len(entries), data_lba, len(found["KERNEL.BIN"])))
    return 0


def fat16(img, fat_off_bytes, index):
    return struct.unpack_from("<H", img, fat_off_bytes + index * 2)[0]


def check_stage1_dap(path):
    dap = LY.g("QIZO_DAP_ADDR")
    if dap % 16:
        return fail("stage1 disk address packet at %x is not paragraph aligned" % dap)
    if not shutil.which("objdump"):
        print("qizo: stage1 disk address packet at %x, alignment ok (no objdump)" % dap)
        return 0
    out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i8086",
                          "--adjust-vma=0x7c00", path],
                         capture_output=True, text=True).stdout
    dests = set()
    for line in out.splitlines():
        line = line.strip()
        if not line or "\t" not in line:
            continue
        text = line.rsplit("\t", 1)[-1]
        if not text.startswith("mov"):
            continue
        tail = text.rsplit(",", 1)[-1].strip()
        if tail.startswith("0x"):
            try:
                dests.add(int(tail, 16))
            except ValueError:
                pass
    for off in (0, 2, 4, 6, 8, 12):
        if dap + off not in dests:
            return fail("stage1 never writes disk address packet field +%d" % off)
    if dap + 10 in dests:
        return fail("stage1 writes the lba high half at +10, seabios reads struct int13ext_s lba at +8")
    flat = out.replace(", ", ",").replace("0x07e00", "0x7e00")
    if ("$0x%x,%%bx" % dap) not in flat:
        return fail("stage1 must hand the disk address packet to INT 13h in es:bx")
    if ("$0x%x,%%si" % dap) not in flat:
        return fail("stage1 must also put the packet in ds:si, which is where seabios reads it")
    if "int" not in out or "$0x13" not in out:
        return fail("stage1 has no int $0x13")
    print("qizo: stage1 disk address packet fields ok at %x" % dap)
    return 0


def decode_desc(b):
    q = int.from_bytes(b, "little")
    return dict(limit=(q & 0xFFFF) | ((q >> 32) & 0xF0000),
                base=((q >> 16) & 0xFFFFFF) | ((q >> 56) & 0xFF) << 24,
                access=(q >> 40) & 0xFF, flags=(q >> 48) & 0xFF,
                g=(q >> 55) & 1, d=(q >> 54) & 1, l=(q >> 53) & 1)


def check_stage2_gdt(data):
    want = {0x08: (0x9A, 0, 1, 0), 0x10: (0x92, 0, 1, 0), 0x18: (0x9A, 0x20000, 0, 0),
            0x28: (0x9A, 0, 1, 1), 0x30: (0x92, 0, 1, 0)}
    gdtr = None
    for i in range(len(data) - 6):
        if struct.unpack_from("<H", data, i)[0] != 55:
            continue
        base = struct.unpack_from("<I", data, i + 2)[0]
        if 0x20000 < base < 0x21000:
            gdtr = base
            break
    if gdtr is None:
        return fail("stage2 has no gdtr pseudo descriptor pointing inside the load area")
    off = gdtr - 0x20000
    for sel in sorted(want):
        access, base_, g_, l_ = want[sel]
        d = decode_desc(data[off + sel:off + sel + 8])
        if d["access"] != access:
            return fail("stage2 gdt selector 0x%02x has access byte 0x%02x, expected 0x%02x"
                        % (sel, d["access"], access))
        if d["base"] != base_:
            return fail("stage2 gdt selector 0x%02x has base 0x%x, expected 0x%x: base 16:23"
                        " occupies byte 4, not the access field" % (sel, d["base"], base_))
        if d["g"] != g_ or d["l"] != l_:
            return fail("stage2 gdt selector 0x%02x has the wrong granularity or mode bits" % sel)
        if not (d["access"] & 0x80) or not (d["access"] & 0x10):
            return fail("stage2 gdt selector 0x%02x is not a present code or data descriptor" % sel)
    return 0


def check_stage2_transition(data):
    if b"\x66\x0f\x01" in data:
        return fail("stage2 descriptor table load carries a 66 prefix, which selects the 10"
                    " byte pseudo descriptor that 16 bit mode cannot use")
    if b"\x0f\x01\x17" not in data:
        return fail("stage2 has no 6 byte lgdt of the real mode form")
    store = data.find(b"\x0f\x22\xc0")
    if store < 0:
        return fail("stage2 never writes cr0, so it cannot enable protected mode")
    read = data.rfind(b"\x0f\x20\xc0", 0, store)
    if read < 0:
        return fail("stage2 writes cr0 without reading it first")
    live = data[read + 3:store]
    for byte, why in ((0xE8, "a call"), (0xB0, "a movb into al"), (0xB2, "a movb into dl")):
        if byte in live:
            return fail("stage2 puts %s inside the cr0 handoff, which destroys the pending"
                        " value in eax" % why)
    if data[store - 2:store] != b"\xc8\x01":
        return fail("stage2 does not set bit 0 of cr0 immediately before the write")
    return 0


def check_iso(iso, image):
    S = 2048
    seen = {}
    lba = 16
    catalog = None
    while True:
        if lba * S + S > len(iso):
            return fail("volume descriptor set runs off the end")
        rec = iso[lba * S:lba * S + S]
        typ = rec[0]
        if typ == 255:
            break
        if typ == 1:
            if rec[1:6] != b"CD001":
                return fail("primary volume descriptor id")
            root = rec[156:156 + 34]
            if len(root) < 34 or root[0] != 34:
                return fail("root directory record malformed")
            seen["root_lba"] = struct.unpack_from("<I", root, 2)[0]
            seen["root_size"] = struct.unpack_from("<I", root, 10)[0]
            seen["block"] = struct.unpack_from("<H", rec, 128)[0]
            if rec[693] != 1:
                return fail("primary volume descriptor file structure version")
            if struct.unpack_from("<H", rec, 120)[0] != 1:
                return fail("primary volume descriptor volume set size")
            if struct.unpack_from(">H", rec, 130) != struct.unpack_from("<H", rec, 128):
                return fail("primary volume descriptor block size is not both-endian")
            if seen["block"] != S:
                return fail("logical block size %d" % seen["block"])
            seen["blocks"] = struct.unpack_from("<I", rec, 80)[0]
        elif typ == 0 and rec[1:6] == b"CD001":
            if rec[7:30] != b"EL TORITO SPECIFICATION".ljust(23, b"\0"):
                return fail("boot record volume descriptor id")
            catalog = struct.unpack_from("<I", rec, 71)[0]
        lba += 1
    if seen.get("block") != S:
        return fail("no primary volume descriptor")
    if catalog is None:
        return fail("no el torito boot volume descriptor")
    cat = iso[catalog * S:catalog * S + S]
    if cat[0] != 1 or cat[1] != 0:
        return fail("boot catalog validation header")
    if cat[30] != 0x55 or cat[31] != 0xAA:
        return fail("boot catalog key")
    acc = 0
    for i in range(0, 32, 2):
        acc = (acc + struct.unpack_from("<H", cat, i)[0]) & 0xFFFF
    if acc:
        return fail("boot catalog checksum does not cancel")
    entry = cat[32:64]
    if entry[0] != 0x88:
        return fail("boot entry not bootable")
    if entry[1] != 4:
        return fail("boot entry is not a hard disk image")
    if struct.unpack_from("<H", entry, 2)[0] != 0x07C0:
        return fail("boot entry load segment")
    if struct.unpack_from("<H", entry, 6)[0] not in (4, 0) and struct.unpack_from("<H", entry, 6)[0] < 66:
        return fail("boot entry load size")
    file_lba = struct.unpack_from("<I", entry, 8)[0]
    if file_lba * S + len(image) > len(iso):
        return fail("boot image runs off the end of the iso")
    if iso[file_lba * S:file_lba * S + len(image)] != image:
        return fail("embedded image bytes differ")
    root_lba, root_size = seen["root_lba"], seen["root_size"]
    root = iso[root_lba * S:root_lba * S + root_size]
    o = 0
    found = None
    while o + 34 <= len(root):
        n = root[o]
        if n == 0:
            break
        idlen = root[o + 32]
        ident = root[o + 33:o + 33 + idlen]
        if ident.startswith(b"QIZO"):
            found = (struct.unpack_from("<I", root, o + 2)[0],
                     struct.unpack_from("<I", root, o + 10)[0])
        o += n
    if not found:
        return fail("QIZO.IMG missing from the root directory")
    if found[0] != file_lba or found[1] != len(image):
        return fail("root directory extent %d/%d != catalog %d/%d" %
                    (found[0], found[1], file_lba, len(image)))
    print("qizo: iso ok: pvd at 16, catalog %d, root %d, image at lba %d" %
          (catalog, root_lba, file_lba))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--stage1")
    ap.add_argument("--stage2")
    ap.add_argument("--kernel-elf")
    ap.add_argument("--iso")
    args = ap.parse_args()

    img = ART.read(args.image)
    if len(img) % SECTOR:
        return fail("image not a whole number of sectors")
    if img[510:512] != b"\x55\xAA":
        return fail("mbr signature missing")
    drive = img[STAGE1_ARGS]
    lba = struct.unpack_from("<I", img, STAGE1_ARGS + 4)[0]
    total = struct.unpack_from("<H", img, STAGE1_ARGS + 8)[0]
    crc = struct.unpack_from("<I", img, STAGE1_ARGS + 12)[0]
    print("qizo: stage1 args: drive=%d blob_lba=%d sectors=%d adler=%08x" %
          (drive, lba, total, crc))
    if lba != LY.PART_LBA:
        return fail("stage1 blob lba is not %d" % LY.PART_LBA)
    if total <= LY.STAGE2_SECTORS:
        return fail("read span does not cover the blob")
    start = lba * SECTOR + LY.STAGE2_BYTES
    blob = img[start:start + (total - LY.STAGE2_SECTORS) * SECTOR]
    if args.stage2:
        want = ART.read(args.stage2).ljust(LY.STAGE2_BYTES, b"\0")
        if img[lba * SECTOR:lba * SECTOR + LY.STAGE2_BYTES] != want:
            return fail("stage2 in the image differs from the built binary")
    if len(blob) != (total - LY.STAGE2_SECTORS) * SECTOR:
        return fail("blob extends past end of image")
    if qizolzss.checksum(blob) != crc:
        return fail("blob adler mismatch")
    if struct.unpack_from("<I", blob, 0)[0] not in (LZSS_MAGIC, 0):
        return fail("blob has neither lzss nor raw header")
    blob_len = struct.unpack_from("<I", blob, 4)[0]
    magic = struct.unpack_from("<I", blob, 0)[0]
    if magic == LZSS_MAGIC:
        out = qizolzss.decompress(blob)
    else:
        out = blob[8:]
    if len(out) != blob_len:
        return fail("decompressed length %d != declared %d" % (len(out), blob_len))
    mod_magic, klen, entry, total_len = struct.unpack_from("<IIII", out, 0)
    if mod_magic != QIZOK_MAGIC:
        return fail("module header magic missing")
    if total_len < klen + 64:
        return fail("module total %u smaller than image %u" % (total_len, klen + 64))
    if total_len > LY.KERNEL_MAX:
        return fail("module total %u exceeds load budget" % total_len)
    if klen + 64 != len(out):
        return fail("module klen mismatch with stream")
    if entry >= klen:
        return fail("entry %d outside kernel" % entry)
    kernel = out[64:64 + klen]
    print("qizo: module ok: kernel %d bytes, entry +%d, blob %d bytes (%.1f%%)" %
          (klen, entry, len(blob), 100.0 * len(blob) / (klen + 64)))
    if klen > LY.KERNEL_MAX:
        return fail("kernel oversized")
    if BLOB_PHYS + len(blob) > E820_BUF:
        return fail("blob overlaps the e820 buffer")
    if E820_BUF + LY.E820_MAX * LY.E820_ENT + 4 > PM_STACK - 0x1000:
        return fail("e820 buffer crowds the 32 bit stack")
    if STAGE2 + STAGE2_BYTES > BLOB_PHYS:
        return fail("stage2 code region does not end before the blob")
    if KTEXT + total_len > SCRATCH:
        return fail("kernel image and bss reach the decompression scratch")
    if SCRATCH + total_len > PAGE_PML4:
        return fail("decompression scratch reaches the page tables")
    if BOOTINFO + BOOTINFO_SIZE > STAGE2:
        return fail("bootinfo reaches the stage2 load area")
    rc = check_fs(img, kernel, args.stage2)
    if rc:
        return rc

    if args.stage1:
        s1 = ART.read(args.stage1)
        if len(s1) != SECTOR:
            return fail("stage1 is not one sector: %d" % len(s1))
        if s1[510:512] != b"\x55\xAA":
            return fail("stage1 signature missing")
        if s1[STAGE1_ARGS + 8:STAGE1_ARGS + 12] != b"\0\0\0\0":
            return fail("stage1 template has a non-zero argument block")
        print("qizo: stage1 is one clean sector, args patched by the image tool")
        if check_stage1_dap(args.stage1):
            return 1
    if args.stage2:
        s2 = ART.read(args.stage2)
        if s2[:3] != b"\xfa\xfc\x31":
            return fail("stage2 prologue missing")
        if check_stage2_gdt(s2):
            return 1
        if check_stage2_transition(s2[:LY.STAGE2_BYTES]):
            return 1
        print("qizo: stage2 %d bytes" % len(s2))
    if args.kernel_elf:
        syms = ART.nm(args.kernel_elf)
        need = ("qizo_kernel_entry", "__qizo_phys_base", "qizo_kmain", "qizo_pg_tables",
                "qizo_idt")
        for n in need:
            if n not in syms:
                return fail("kernel elf missing symbol " + n)
        print("qizo: kernel elf symbols present: %s" % ", ".join(need))
    if args.iso:
        rc = check_iso(ART.read(args.iso), img)
        if rc:
            return rc

    print("qizo: check OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
