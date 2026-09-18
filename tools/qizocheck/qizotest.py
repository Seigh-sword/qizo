import os
import random
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import qizolzss
import qizolayout as LY
import qizofont

fails = []


def check(name, cond):
    if cond:
        print("ok   %s" % name)
    else:
        fails.append(name)
        print("FAIL %s" % name)


def hand_adler(data):
    a, b = 1, 0
    for byte in data:
        a = (a + byte) % 65521
        b = (b + a) % 65521
    return ((b << 16) | a) & 0xFFFFFFFF


def test_checksum():
    data = bytes(range(251)) * 40 + b"qizo"
    check("adler matches the hand loop", qizolzss.checksum(data) == hand_adler(data))
    check("adler of empty", qizolzss.checksum(b"") == 1)
    rnd = random.Random(7)
    for i in range(4):
        blob = bytes(rnd.randrange(256) for _ in range(1 + i * 977))
        check("adler stable for random blob %d" % i, qizolzss.checksum(blob) == hand_adler(blob))


def test_lzss():
    rnd = random.Random(1234)
    cases = [b"", b"a", b"ab", b"\0" * 5000, bytes(range(256)),
             b"qizo" * 4000, bytes(rnd.randrange(256) for _ in range(9000))]
    for i in range(6):
        chunk = bytes(rnd.choice(b"ACGT") for _ in range(4000))
        cases.append(chunk + chunk[:200] + chunk)
    for i, src in enumerate(cases):
        blob = qizolzss.compress(src)
        back = qizolzss.decompress(blob)
        check("lzss roundtrip %d (%d bytes)" % (i, len(src)), back == src)
        check("lzss header %d" % i, struct.unpack_from("<I", blob, 0)[0] == 0x51495A4C)
    worst = bytes(rnd.randrange(256) for _ in range(20000))
    blob = qizolzss.compress(worst)
    check("lzss never explodes past the 16-bit window", len(blob) < len(worst) + 20000 // 8 * 2 + 16)


def test_match_window():
    src = b"qizo" * 5000
    blob = qizolzss.compress(src)
    check("lzss shrinks repetitive input", len(blob) * 3 < len(src) * 2)


def test_layout():
    check("stage2 base below the blob", LY.STAGE2 < LY.BLOB_PHYS)
    check("blob fits its span", LY.BLOB_PHYS + LY.BLOB_MAX <= LY.E820_BUF)
    check("kernel plus bss below scratch", LY.KTEXT + LY.KERNEL_MAX <= LY.SCRATCH)
    check("page tables above scratch", LY.PAGE_PML4 >= LY.SCRATCH + LY.KERNEL_MAX + LY.QIZOK_HDR)
    check("bootinfo clear of the stack",
          LY.BOOTINFO + LY.BOOTINFO_SIZE <= LY.E820_BUF)
    check("bootinfo field offsets ascend",
          all(LY.BI[a] < LY.BI[b] for a, b in zip(sorted(LY.BI, key=LY.BI.get),
                                                   sorted(LY.BI, key=LY.BI.get)[1:])))
    check("blob chunking covers the span", (LY.BLOB_MAX + 511) // 512 <= 256)


def test_font():
    rows = qizofont.bitmap(ord("A"))
    check("font glyph rows", len(rows) == qizofont.H)
    check("font has A", any(rows))
    check("font blank space", qizofont.bitmap(ord(" ")) == [0] * qizofont.H)


if __name__ == "__main__":
    test_checksum()
    test_lzss()
    test_match_window()
    test_layout()
    test_font()
    if fails:
        print("qizo: %d test(s) failed: %s" % (len(fails), ", ".join(fails)))
        raise SystemExit(1)
    print("qizo: all tests pass")
