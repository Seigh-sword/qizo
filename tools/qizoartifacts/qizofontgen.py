import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import qizofont
import qizoartifacts as ART


def glyph_rows(ch):
    out = []
    for row in range(16):
        gy = row // 2 - 2
        v = 0
        if 0 <= gy < qizofont.H:
            for col in range(8):
                gx = col - 1
                if 0 <= gx < qizofont.W and qizofont.pixel(ch, gx, gy):
                    v |= 1 << (7 - col)
        out.append(v)
    return out


def emit_array(path):
    rows = []
    for ch in range(256):
        src = ch + qizofont.FIRST
        data = glyph_rows(src) if qizofont.FIRST <= src <= qizofont.LAST else [0] * 16
        rows.extend(data)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        f.write("#pragma once\n")
        f.write("static const unsigned char qizo_font_data[256 * 16] = {\n")
        for i in range(0, len(rows), 16):
            f.write("\t" + ",".join("0x%02x" % v for v in rows[i:i + 16]) + ",\n")
        f.write("};\n")
    return len(rows)


def emit_psf1(path):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    data = bytearray(b"\x72\xb5\x4a\x86\x00\x08\x10\x00")
    for ch in range(256):
        src = ch + qizofont.FIRST
        rows = glyph_rows(src) if qizofont.FIRST <= src <= qizofont.LAST else [0] * 16
        data += bytes(rows)
    ART.write(path, bytes(data))
    return len(data)


if __name__ == "__main__":
    out = sys.argv[1]
    n1 = emit_array(out)
    if len(sys.argv) > 2:
        n2 = emit_psf1(sys.argv[2])
        print("font: array rows %d, psf1 %s" % (n1, ART.human(n2)))
    else:
        print("font: array rows %d" % n1)
