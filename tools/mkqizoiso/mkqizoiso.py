import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "qizoartifacts"))

import qizoartifacts as ART

SECTOR = 2048
PVD_LBA = 16
BRVD_LBA = 17
TERM_LBA = 18
CATALOG_LBA = 19
ROOT_LBA = 20
FILE_LBA = 21
FILE_NAME = "QIZO.IMG;1"
DATE = b"20260101000000000"


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def dir_record(lba, size, flags, ident):
    body = bytearray()
    body.append(0)
    body.append(0)
    body += both32(lba)
    body += both32(size)
    body += bytes([26, 1, 1, 0, 0, 0, 0])
    body.append(flags)
    body.append(0)
    body.append(0)
    body += both16(1)
    body.append(len(ident))
    body += ident
    if len(body) % 2:
        body.append(0)
    body[0] = len(body)
    return bytes(body)


def text_field(buf, off, n, value):
    buf[off:off + n] = value.encode()[:n].ljust(n, b" ")


def build_iso(image, out, volume="QIZO"):
    payload = bytearray(image)
    if len(payload) % SECTOR:
        payload += b"\0" * (SECTOR - len(payload) % SECTOR)
    file_sectors = len(payload) // SECTOR

    root_body = bytearray()
    root_body += dir_record(ROOT_LBA, 0, 1 | 2, b"\0")
    root_body += dir_record(ROOT_LBA, 0, 1 | 2, b"\1")
    root_body += dir_record(FILE_LBA, len(image), 5, FILE_NAME.encode())
    root_body += b"\0" * (SECTOR - len(root_body) % SECTOR)
    root_sectors = len(root_body) // SECTOR
    total = FILE_LBA + file_sectors

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b" " * 32
    pvd[40:72] = volume.encode()[:32].ljust(32, b" ")
    pvd[80:88] = both32(total)
    pvd[88:92] = both16(1)
    pvd[92:96] = both16(1)
    pvd[96:100] = both16(SECTOR)
    pvd[100:108] = both32(0)
    pvd[108:112] = struct.pack("<I", 0)
    pvd[112:116] = struct.pack("<I", 0)
    pvd[116:120] = struct.pack("<I", 0)
    pvd[120:124] = struct.pack("<I", 0)
    root_rec = dir_record(ROOT_LBA, len(root_body), 1 | 2, b"\0")
    pvd[124:124 + len(root_rec)] = root_rec
    text_field(pvd, 158, 128, "QIZO")
    text_field(pvd, 286, 128, "ARENA AI AGENT MODE")
    text_field(pvd, 414, 128, "ARENA AI AGENT MODE")
    text_field(pvd, 542, 128, "QIZO MKQIZOISO")
    for off in (670, 707, 744):
        text_field(pvd, off, 37, "")
    for off in (781, 798, 815, 832):
        pvd[off:off + 17] = DATE
    pvd[881] = 1
    pvd[882] = 0
    pvd[883:1395] = b" " * 512
    pvd[1395:SECTOR] = b"\0" * (SECTOR - 1395)

    brvd = bytearray(SECTOR)
    brvd[0] = 0
    brvd[1:5] = b"CD00"
    brvd[5] = 1
    brvd[6:38] = b"EL TORITO SPECIFICATION".ljust(32, b" ")
    struct.pack_into("<I", brvd, 38, CATALOG_LBA)
    struct.pack_into("<I", brvd, 7, 0)

    terminator = bytearray(SECTOR)
    terminator[0] = 255
    terminator[1:6] = b"CD001"
    terminator[6] = 1

    entry = bytearray(32)
    entry[0] = 0x88
    entry[1] = 0
    entry[2:4] = struct.pack("<H", 0x07C0)
    entry[4] = 0
    entry[5] = 0
    entry[6:8] = struct.pack("<H", 0)
    entry[8:12] = struct.pack("<I", FILE_LBA)
    entry[12:16] = struct.pack("<I", 0)
    entry[16:20] = struct.pack("<I", (len(image) + 511) // 512)
    entry[20] = 0
    entry[21] = 0
    entry[22:24] = struct.pack("<H", 0)
    entry[24:26] = struct.pack("<H", 0)
    entry[26:32] = b"\0" * 6
    acc = 0
    for i in range(0, 32, 2):
        if i == 22:
            continue
        acc = (acc + struct.unpack_from("<H", entry, i)[0]) & 0xFFFF
    entry[22:24] = struct.pack("<H", (-acc) & 0xFFFF)

    cat = bytearray(SECTOR)
    cat[0] = 0x01
    cat[1] = 0xEF
    cat[2:28] = b"QIZO".ljust(26, b" ")
    cat[30] = 0x55
    cat[31] = 0xAA
    cat[32:34] = struct.pack("<H", 1)
    cat[34:36] = struct.pack("<H", 0)
    cat[36:40] = b"\x56\x43\x01\x00"
    cat[64:64 + 32] = bytes(entry)
    struct.pack_into("<H", cat, 28, (cat[0] + cat[1] + sum(cat[2:28])) & 0xFFFF)

    out_bytes = bytearray()
    out_bytes += b"\0" * (PVD_LBA * SECTOR)
    out_bytes += bytes(pvd)
    out_bytes += bytes(brvd)
    out_bytes += bytes(terminator)
    out_bytes += bytes(cat)
    out_bytes += bytes(root_body)
    out_bytes += bytes(payload)
    for name, blk in (("pvd", pvd), ("brvd", brvd), ("terminator", terminator),
                      ("catalog", cat), ("root", root_body)):
        if len(blk) % SECTOR:
            raise SystemExit("qizo: %s block is %d bytes, not a whole number of sectors" %
                             (name, len(blk)))
    if len(out_bytes) != total * SECTOR:
        raise SystemExit("qizo: iso is %d bytes but the layout promised %d sectors" %
                         (len(out_bytes), total))
    ART.write(out, bytes(out_bytes))
    return dict(sectors=len(out_bytes) // SECTOR, catalog=CATALOG_LBA, root_lba=ROOT_LBA,
                file_lba=FILE_LBA, file_sectors=file_sectors, image=len(image))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--volume", default="QIZO")
    args = ap.parse_args()
    info = build_iso(ART.read(args.image), args.out, args.volume)
    print("qizo: iso %d sectors, catalog %d, root %d, boot image at %d (%d sectors), %s" %
          (info["sectors"], info["catalog"], info["root_lba"], info["file_lba"],
           info["file_sectors"], ART.human(os.path.getsize(args.out))))
