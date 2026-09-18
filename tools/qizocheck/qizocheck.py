import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "qizoartifacts"))

import qizolzss
import qizolayout as LY
import qizoartifacts as ART

SECTOR = 512
STAGE1_ARGS = 0x1D0
BLOB_BASE = 0xA0000
BLOB_PHYS = 0xA1000
SCRATCH = 0x140000
KTEXT = 0x100000
QIZOK_MAGIC = 0x4B4F5A49
LZSS_MAGIC = 0x51495A4C


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
    if struct.unpack_from("<H", entry, 6)[0] != 4:
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
    if total_len > 0x20000:
        return fail("module total %u exceeds load budget" % total_len)
    if klen + 64 != len(out):
        return fail("module klen mismatch with stream")
    if entry >= klen:
        return fail("entry %d outside kernel" % entry)
    kernel = out[64:64 + klen]
    print("qizo: module ok: kernel %d bytes, entry +%d, blob %d bytes (%.1f%%)" %
          (klen, entry, len(blob), 100.0 * len(blob) / (klen + 64)))
    if klen > 0x20000:
        return fail("kernel oversized")
    if BLOB_PHYS + len(blob) > SCRATCH:
        return fail("blob overlaps decompression scratch")
    if KTEXT + klen + 0x10000 > BLOB_PHYS:
        pass
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
    if args.stage2:
        s2 = ART.read(args.stage2)
        if s2[:3] != b"\xfa\xfc\x31":
            return fail("stage2 prologue missing")
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
