import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "qizoartifacts"))

import qizolzss
import qizolayout as LY
import qizoartifacts as ART


class Mem:
    def __init__(self, size=0x4000000):
        self.buf = bytearray(size)

    def read(self, addr, n):
        return bytes(self.buf[addr:addr + n])

    def write(self, addr, data):
        self.buf[addr:addr + len(data)] = data

    def u8(self, addr):
        return self.buf[addr]

    def u16(self, addr):
        return struct.unpack_from("<H", self.buf, addr)[0]

    def u32(self, addr):
        return struct.unpack_from("<I", self.buf, addr)[0]

    def put_u32(self, addr, v):
        struct.pack_into("<I", self.buf, addr, v)

    def put_u16(self, addr, v):
        struct.pack_into("<H", self.buf, addr, v)

    def put_u8(self, addr, v):
        self.buf[addr] = v & 0xFF


def int13_read(mem, disk, dap):
    count = mem.u16(dap + 2)
    off = mem.u16(dap + 4)
    seg = mem.u16(dap + 6)
    lba = mem.u32(dap + 8)
    dest = seg * 16 + off
    src = disk[lba * LY.SECTOR:lba * LY.SECTOR + count * LY.SECTOR]
    if len(src) != count * LY.SECTOR:
        raise SystemExit("model: disk underrun at lba %d" % lba)
    mem.write(dest, src)
    return count, dest


def stage1(mem, disk, drive):
    mem.write(0x7C00, disk[0:512])
    mem.put_u8(LY.QIZO["QIZO_ARG_ADDR"] + LY.QIZO["QIZO_ARG_DRIVE"], drive)
    bx = 0
    total = mem.u16(LY.QIZO["QIZO_ARG_ADDR"] + LY.QIZO["QIZO_ARG_TOTAL"])
    while bx < total:
        count = min(64, total - bx)
        mem.put_u16(LY.QIZO["QIZO_DAP_ADDR"] + 2, count)
        mem.put_u16(LY.QIZO["QIZO_DAP_ADDR"] + 4, 0)
        mem.put_u16(LY.QIZO["QIZO_DAP_ADDR"] + 6, (LY.STAGE2 >> 4) + bx * 64)
        mem.put_u32(LY.QIZO["QIZO_DAP_ADDR"] + 8,
                    mem.u32(LY.QIZO["QIZO_ARG_ADDR"] + LY.QIZO["QIZO_ARG_LBA"]) + bx)
        mem.put_u32(LY.QIZO["QIZO_DAP_ADDR"] + 12, 0)
        n, dest = int13_read(mem, disk, LY.QIZO["QIZO_DAP_ADDR"])
        if dest + n * LY.SECTOR > LY.BLOB_PHYS + LY.BLOB_MAX:
            raise SystemExit("model: read overruns the blob region")
        bx += n
    return bx * LY.SECTOR


def stage2_real(mem):
    arg = LY.QIZO["QIZO_ARG_ADDR"]
    bi = LY.QIZO["QIZO_BOOTINFO"]
    mem.put_u32(bi + LY.QIZO["QIZO_BI_MAGIC"], 0x00010001)
    mem.put_u8(bi + LY.QIZO["QIZO_BI_DRIVE"], mem.u8(arg + 0))
    mem.put_u8(bi + LY.QIZO["QIZO_BI_MODE"], 1)
    mem.put_u32(bi + LY.QIZO["QIZO_BI_BLOB_LBA"], mem.u32(arg + 4))
    mem.put_u16(bi + LY.QIZO["QIZO_BI_BLOB_SECTORS"], mem.u16(arg + 8))
    mem.put_u32(bi + LY.QIZO["QIZO_BI_BLOB_SIZE"],
                mem.u16(arg + LY.QIZO["QIZO_ARG_TOTAL"]) * LY.SECTOR - LY.STAGE2_BYTES)
    mem.put_u32(bi + LY.QIZO["QIZO_BI_BLOB_PHYS"], LY.BLOB_PHYS)
    mem.put_u32(bi + LY.QIZO["QIZO_BI_BLOB_CRC"], mem.u32(arg + 12))
    mem.put_u32(bi + LY.QIZO["QIZO_BI_SCRATCH"], LY.SCRATCH)
    mem.put_u32(bi + LY.QIZO["QIZO_BI_KTEXT"], LY.KTEXT)
    mem.put_u32(bi + LY.QIZO["QIZO_BI_FLAGS"], LY.QIZO["QIZO_BOOTVER"])
    mem.put_u8(bi + LY.QIZO["QIZO_BI_A20"], 1)
    mem.put_u8(bi + LY.QIZO["QIZO_BI_LONGMODE"], 1)


def stage2_pm32(mem):
    bi = LY.QIZO["QIZO_BOOTINFO"]
    blob = mem.read(LY.BLOB_PHYS, mem.u32(bi + LY.QIZO["QIZO_BI_BLOB_SIZE"]))
    if zlib.adler32(blob, 1) & 0xFFFFFFFF != mem.u32(bi + LY.QIZO["QIZO_BI_BLOB_CRC"]):
        raise SystemExit("model: stage2 adler mismatch")
    out = qizolzss.decompress(blob)
    mem.write(LY.SCRATCH, out)
    magic, klen, entry, total = struct.unpack("<IIII", mem.read(LY.SCRATCH, 16))
    if magic != LY.QIZOK_MAGIC:
        raise SystemExit("model: module magic missing")
    if klen > LY.KERNEL_MAX or total > LY.KERNEL_MAX or entry >= klen:
        raise SystemExit("model: module header out of range")
    if klen + 64 != len(out):
        raise SystemExit("model: decompressed size mismatch")
    image = mem.read(LY.SCRATCH + LY.QIZOK_HDR, klen)
    mem.write(LY.KTEXT, image)
    mem.write(LY.KTEXT + klen, b"\0" * (total - klen))
    return klen, entry, total


def regions(klen, total):
    return [
        ("stage2+blob", LY.STAGE2, LY.BLOB_PHYS + LY.BLOB_MAX),
        ("e820", LY.E820_BUF, LY.E820_BUF + LY.E820_MAX * 24 + 8),
        ("pmstack", LY.PM_STACK - 0x1000, LY.PM_STACK),
        ("bootinfo", LY.QIZO["QIZO_BOOTINFO"], LY.QIZO["QIZO_BOOTINFO"] + LY.BOOTINFO_SIZE),
        ("kernel", LY.KTEXT, LY.KTEXT + max(klen, total)),
        ("scratch", LY.SCRATCH, LY.SCRATCH + LY.KERNEL_MAX + 64),
        ("pages", LY.PAGE_PML4, LY.PAGE_PDPT + 2048 * 8),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--kernel-bin", required=True)
    args = ap.parse_args()
    disk = ART.read(args.image)
    kernel = ART.read(args.kernel_bin)
    mem = Mem()
    LY.QIZO = LY.parse()
    LY.STAGE2_BYTES = LY.LAYOUT['QIZO_STAGE2_BYTES']
    LY.STAGE1_ARGS_OFF = LY.QIZO["QIZO_BIOS_ARG"] - 0x7C00
    sectors = stage1(mem, disk, 0x80)
    if sectors <= LY.STAGE2_BYTES:
        raise SystemExit("model: nothing to decompress")
    stage2_real(mem)
    klen, entry, total = stage2_pm32(mem)
    got = mem.read(LY.KTEXT, klen)
    if got != kernel:
        pos = next(i for i in range(len(kernel)) if got[i] != kernel[i]) if len(got) == len(kernel) else min(len(got), len(kernel))
        raise SystemExit("model: kernel at 0x%x differs at +%d" % (LY.KTEXT, pos))
    if mem.u8(LY.KTEXT) != kernel[0]:
        raise SystemExit("model: entry byte mismatch")
    rs = regions(klen, total)
    rs.sort(key=lambda r: r[1])
    for a, b in zip(rs, rs[1:]):
        if b[1] < a[2]:
            raise SystemExit("model: %s [%x,%x) overlaps %s [%x,%x)" %
                             (a[0], a[1], a[2], b[0], b[1], b[2]))
    print("model: read %d sectors, kernel %d bytes at 0x%x entry +%d, image+bss %d KiB" %
          (sectors, klen, LY.KTEXT, entry, total // 1024))
    print("model: boot regions disjoint, handoff verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
