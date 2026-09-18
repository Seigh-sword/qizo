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
    root_body += dir_record(ROOT_LBA, 0, 2, b"\0")
    root_body += dir_record(ROOT_LBA, 0, 2, b"\1")
    root_body += dir_record(FILE_LBA, len(image), 0, FILE_NAME.encode())
    root_body += b"\0" * (SECTOR - len(root_body) % SECTOR)
    root_sectors = len(root_body) // SECTOR
    total = FILE_LBA + file_sectors

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"QIZO".ljust(32, b" ")
    pvd[40:72] = volume.encode()[:32].ljust(32, b" ")
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(0)
    pvd[140:144] = struct.pack("<I", 0)
    pvd[144:148] = struct.pack("<I", 0)
    pvd[148:152] = struct.pack("<I", 0)
    pvd[152:156] = struct.pack("<I", 0)
    root_rec = dir_record(ROOT_LBA, len(root_body), 2, b"\0")
    pvd[156:156 + len(root_rec)] = root_rec
    text_field(pvd, 190, 128, "QIZO")
    text_field(pvd, 318, 128, "ARENA AI AGENT MODE")
    text_field(pvd, 446, 128, "ARENA AI AGENT MODE")
    for off in (582, 619, 656):
        pvd[off] = 0
    pvd[693] = 1
    text_field(pvd, 702, 32, "QIZO MKQIZOISO")
    text_field(pvd, 734, 32, "ARENA AI AGENT MODE")
    text_field(pvd, 766, 32, "SEIGH-SWORD")
    text_field(pvd, 798, 32, "SURIEWEPEDI")
    for off in (830, 847, 864, 881):
        pvd[off:off + 17] = DATE
    pvd[898:SECTOR] = b"\0" * (SECTOR - 898)

    brvd = bytearray(SECTOR)
    brvd[0] = 0
    brvd[1:6] = b"CD001"
    brvd[6] = 1
    brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    struct.pack_into("<I", brvd, 71, CATALOG_LBA)

    terminator = bytearray(SECTOR)
    terminator[0] = 255
    terminator[1:6] = b"CD001"
    terminator[6] = 1

    entry = bytearray(32)
    entry[0] = 0x88
    entry[1] = 4
    struct.pack_into("<H", entry, 2, 0x07C0)
    entry[4] = 0
    entry[5] = 0
    struct.pack_into("<H", entry, 6, 4)
    struct.pack_into("<I", entry, 8, FILE_LBA)
    struct.pack_into("<I", entry, 12, 0)

    cat = bytearray(SECTOR)
    cat[0] = 0x01
    cat[1] = 0
    cat[4:28] = b"QIZO MKQIZOISO".ljust(24, b"\0")
    cat[30] = 0x55
    cat[31] = 0xAA
    acc = 0
    for i in range(0, 32, 2):
        if i == 28:
            continue
        acc = (acc + struct.unpack_from("<H", cat, i)[0]) & 0xFFFF
    struct.pack_into("<H", cat, 28, (-acc) & 0xFFFF)
    cat[32:64] = bytes(entry)

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
