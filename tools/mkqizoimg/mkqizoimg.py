import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "qizoartifacts"))

import qizolzss
import qizolayout as LY
import qizoartifacts as ART

SECTOR = LY.SECTOR
STAGE1_ARGS_OFF = LY.STAGE1_ARGS_OFF
QIZOK_MAGIC = LY.QIZOK_MAGIC
PART_LBA = LY.PART_LBA
BLOB_LBA = LY.BLOB_LBA
BLOB_MAX = LY.BLOB_MAX
STAGE2_SECTORS = LY.STAGE2_SECTORS
STAGE2_LBA = LY.PART_LBA
STAGE2_BYTES = LY.STAGE2_BYTES


def fat_size_for(clusters, bits, sector=SECTOR):
    if bits == 12:
        return max(1, ((clusters * 3) + sector * 8 - 1) // (sector * 8))
    if bits == 16:
        return max(1, (clusters * 16 + sector * 8 - 1) // (sector * 8))
    return max(1, (clusters * 32 + sector * 8 - 1) // (sector * 8))


def plan_layout(total_sectors, part_lba, reserved, bits):
    minimum = 4085 if bits == 16 else 4085
    root_sectors = ROOT_SECTORS[bits]
    fat_size = 1
    for _ in range(48):
        data_sectors = total_sectors - part_lba - reserved - 2 * fat_size - root_sectors
        clusters = data_sectors
        need = fat_size_for(clusters + 2, bits)
        if need <= fat_size:
            break
        fat_size = need
    data_sectors = total_sectors - part_lba - reserved - 2 * fat_size - root_sectors
    clusters = data_sectors
    if clusters < minimum:
        return None
    return dict(bits=bits, fat_size=fat_size, clusters=clusters, reserved=reserved,
                root_sectors=root_sectors, part_lba=part_lba)


ROOT_SECTORS = {12: 14, 16: 14, 32: 0}
RESERVED = 32


def pick_layout(blob_sectors):
    part_lba = BLOB_LBA + blob_sectors
    bits = 16
    total = part_lba + RESERVED + 2 + 14 + 4096
    while total < 1 << 22:
        layout = plan_layout(total, part_lba, RESERVED, bits)
        if layout:
            return total, layout
        total += 256
    raise SystemExit("qizo: unable to lay out a filesystem image")


def dir_entry(name, size, cluster, attrs=0x20, modtime=0x4000, moddate=0x5C61):
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    base = base.upper()[:8].ljust(8, " ")
    ext = ext.upper()[:3].ljust(3, " ")
    e = bytearray(32)
    e[0:8] = base.encode()
    e[8:11] = ext.encode()
    e[11] = attrs
    e[12] = 0
    e[13] = 0
    e[14:16] = struct.pack("<H", modtime)
    e[16:18] = struct.pack("<H", moddate)
    e[18:20] = struct.pack("<H", moddate)
    e[20:22] = struct.pack("<H", 0)
    e[22:24] = struct.pack("<H", modtime)
    e[24:26] = struct.pack("<H", moddate)
    e[26:28] = struct.pack("<H", cluster & 0xFFFF)
    e[28:32] = struct.pack("<I", size)
    return bytes(e)


def build_image(total_sectors, layout, files, label, blob, blob_offset, stage2_img):
    bits = layout["bits"]
    reserved = layout["reserved"]
    fat_size = layout["fat_size"]
    part = layout["part_lba"]
    img = bytearray(total_sectors * SECTOR)
    bs = bytearray(512)
    bs[0:3] = b"\xEB\x3E\x90"
    bs[3:11] = b"QIZOIMG\x00"
    struct.pack_into("<H", bs, 11, SECTOR)
    bs[13] = 1
    struct.pack_into("<H", bs, 14, reserved)
    bs[16] = 2
    struct.pack_into("<H", bs, 17, 224)
    struct.pack_into("<H", bs, 19, 0)
    bs[21] = 0xF8
    struct.pack_into("<H", bs, 22, fat_size)
    struct.pack_into("<H", bs, 24, 63)
    struct.pack_into("<H", bs, 26, 255)
    struct.pack_into("<I", bs, 28, part)
    struct.pack_into("<I", bs, 32, total_sectors - part)
    bs[34] = 0x80
    bs[35] = 0x01
    struct.pack_into("<H", bs, 36, 0)
    struct.pack_into("<H", bs, 38, total_sectors - PART_LBA)
    bs[42] = 0x29
    struct.pack_into("<I", bs, 43, 0x51495A4F)
    bs[47:58] = label.encode()[:11].ljust(11, b" ")
    fatname = (b"FAT16   " if bits == 16 else b"FAT12   ")
    bs[58:66] = fatname
    bs[66:68] = b"\x29\x00"
    bs[510:512] = b"\x55\xAA"
    p0 = part * SECTOR
    img[p0:p0 + 512] = bs
    img[STAGE2_LBA * SECTOR:STAGE2_LBA * SECTOR + len(stage2_img)] = stage2_img

    fat = bytearray(fat_size * SECTOR)
    eof = 0xFFF if bits == 12 else 0xFFFF
    first_data_cluster = 2
    cluster_of = {}
    nxt = first_data_cluster
    for name, payload in files:
        size = len(payload)
        count = (size + SECTOR - 1) // SECTOR
        if count:
            cluster_of[name] = (nxt, count)
            for i in range(count - 1):
                fat_set(fat, bits, nxt + i, nxt + i + 1)
            fat_set(fat, bits, nxt + count - 1, eof)
            nxt += count
    fat_set(fat, bits, 0, eof if bits == 16 else 0xF8)
    fat_set(fat, bits, 1, eof)
    root_sectors = ROOT_SECTORS[bits]
    root = bytearray(root_sectors * SECTOR)
    off = 0
    root[off:off + 32] = dir_entry(label, 0, 0, attrs=0x08)
    off += 32
    for name, payload in files:
        first, _ = cluster_of.get(name, (0, 0))
        root[off:off + 32] = dir_entry(name, len(payload), first)
        off += 32
    fat_lba = part + reserved
    root_lba = fat_lba + 2 * fat_size
    data_lba = root_lba + root_sectors
    img[fat_lba * SECTOR:fat_lba * SECTOR + len(fat)] = bytes(fat)
    img[(fat_lba + fat_size) * SECTOR:(fat_lba + fat_size) * SECTOR + len(fat)] = bytes(fat)
    img[root_lba * SECTOR:root_lba * SECTOR + len(root)] = root
    for name, payload in files:
        if name not in cluster_of:
            continue
        first, count = cluster_of[name]
        lba = data_lba + (first - 2)
        img[lba * SECTOR:lba * SECTOR + len(payload)] = payload
    img[blob_offset:blob_offset + len(blob)] = blob
    return bytes(img), dict(fat_lba=fat_lba, root_lba=root_lba, data_lba=data_lba,
                            fatname=fatname.decode(), clusters=nxt - 2)


def fat_set(fat, bits, index, value):
    if bits == 12:
        off = index * 3 // 2
        if index & 1:
            fat[off] = (fat[off] & 0x0F) | ((value & 0x0F) << 4)
            fat[off + 1] = (value >> 4) & 0xFF
        else:
            fat[off] = value & 0xFF
            fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)
    else:
        struct.pack_into("<H", fat, index * 2, value & 0xFFFF)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage1", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel-bin", required=True)
    ap.add_argument("--kernel-elf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--entry", type=lambda x: int(x, 0))
    ap.add_argument("--image-sectors", type=int, default=0)
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--label", default="QIZO_BOOT")
    args = ap.parse_args()

    stage1 = bytearray(ART.read(args.stage1))
    args_off = LY.STAGE1_ARGS_OFF
    if bytes(stage1[args_off:args_off + 20]) != b"\0" * 20:
        raise SystemExit("qizo: stage1 argument block is not clear")
    if len(stage1) < 512:
        stage1 += bytearray(512 - len(stage1))
    if stage1[510:512] != b"\x55\xAA":
        raise SystemExit("qizo: stage1 missing boot signature")

    stage2 = ART.read(args.stage2)
    if len(stage2) > STAGE2_BYTES:
        raise SystemExit("qizo: stage2 code %d bytes exceeds %d" % (len(stage2), STAGE2_BYTES))
    kernel = ART.read(args.kernel_bin)
    syms = ART.nm(args.kernel_elf)
    base = syms["__qizo_phys_base"]
    entry = args.entry if args.entry is not None else syms.get("qizo_kernel_entry")
    if entry is None:
        raise SystemExit("qizo: cannot determine kernel entry")
    entry_off = entry - base
    image_end = syms.get("__qizo_image_end", base + len(kernel))
    bss_end = syms.get("__qizo_bss_end", image_end)
    klen = image_end - base
    img_total = max(bss_end, image_end) - base
    if klen != len(kernel):
        raise SystemExit("qizo: kernel file %d bytes but image span %d" % (len(kernel), klen))
    if klen + LY.QIZOK_HDR > LY.KERNEL_MAX or img_total > LY.KERNEL_MAX:
        raise SystemExit("qizo: kernel %d bytes exceeds the %d byte load budget" %
                         (klen, LY.KERNEL_MAX))
    mod = struct.pack("<IIIIII", QIZOK_MAGIC, klen, entry_off, img_total, base, SECTOR)
    mod += b"\0" * (64 - len(mod))
    payload = mod + kernel
    if args.raw:
        blob = struct.pack("<II", 0, len(payload)) + payload
    else:
        blob = qizolzss.compress(payload)
    if len(blob) > BLOB_MAX:
        raise SystemExit("qizo: blob %d bytes exceeds %d" % (len(blob), BLOB_MAX))
    blob_sectors = (len(blob) + SECTOR - 1) // SECTOR
    blob = blob + b"\0" * (blob_sectors * SECTOR - len(blob))
    crc = qizolzss.checksum(blob)

    struct.pack_into("<I", stage1, STAGE1_ARGS_OFF + 4, PART_LBA)
    struct.pack_into("<H", stage1, STAGE1_ARGS_OFF + 8, STAGE2_SECTORS + blob_sectors)
    struct.pack_into("<I", stage1, STAGE1_ARGS_OFF + 12, crc)

    stage2_img = stage2 + b"\0" * (STAGE2_SECTORS * SECTOR - len(stage2))
    blob_offset = BLOB_LBA * SECTOR
    part_lba = BLOB_LBA + blob_sectors
    if args.image_sectors:
        total = args.image_sectors
        layout = plan_layout(total, part_lba, RESERVED, 16) or \
            plan_layout(total, part_lba, RESERVED, 12)
        if not layout:
            raise SystemExit("qizo: image too small for a valid FAT volume")
    else:
        total, layout = pick_layout(blob_sectors)
    readme = []
    readme.append("qizo bootable image\n")
    readme.append("stage2 at lba %d (%d sectors), module blob at lba %d (%d sectors)\n" %
                  (STAGE2_LBA, STAGE2_SECTORS, BLOB_LBA, blob_sectors))
    readme.append("kernel image %d bytes, entry +%d, image plus bss %d KiB\n" %
                  (len(kernel), entry_off, (img_total + 1023) // 1024))
    readme.append("compressed payload %d bytes, ratio %.1f%%\n" %
                  (len(blob), 100.0 * len(blob) / len(payload)))
    readme.append("filesystem: FAT%s, %d clusters of %d sector(s), reserved lba 0..%d\n" %
                  ("16" if layout["bits"] == 16 else "12", layout["clusters"], 1,
                   part_lba - 1))
    files = [("README.TXT", "".join(readme).encode()), ("KERNEL.BIN", kernel)]
    img, info = build_image(total, layout, files, args.label, blob, blob_offset, stage2_img)
    img = bytearray(img)
    img[0:SECTOR] = stage1
    type_id = 0x06 if layout["bits"] == 16 else 0x01
    pe = 0x1BE
    img[pe] = 0x80
    img[pe + 1:pe + 4] = b"\x00\x02\x01"
    img[pe + 4] = type_id
    img[pe + 5:pe + 8] = b"\xFE\xFF\xFF"
    struct.pack_into("<I", img, pe + 8, part_lba)
    struct.pack_into("<I", img, pe + 12, total - part_lba)
    ART.write(args.out, bytes(img))
    print("qizo: wrote %s (%s)" % (args.out, ART.human(len(img))))
    print("qizo: blob %d bytes in %d sectors, adler %08x, ratio %.1f%%" %
          (len(blob), blob_sectors, crc, 100.0 * len(blob) / len(payload)))
    print("qizo: fat FAT%s volume clusters %d used %d part_lba %d sectors %d" %
          ("16" if layout["bits"] == 16 else "12", layout["clusters"], info["clusters"],
           part_lba, total))


if __name__ == "__main__":
    main()
