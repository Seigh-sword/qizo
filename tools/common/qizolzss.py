import struct
import zlib


LZSS_MAGIC = 0x51495A4C
WINDOW = 4096
MIN_MATCH = 3
MAX_MATCH = 18


def checksum(data):
    return zlib.adler32(data, 1) & 0xFFFFFFFF


def compress(src):
    out = bytearray(struct.pack("<II", LZSS_MAGIC, len(src)))
    n = len(src)
    i = 0
    while i < n:
        fpos = len(out)
        out += b"\0"
        flags = 0
        for bit in range(8):
            if i >= n:
                break
            off, ln = _find_match(src, i, n)
            if ln:
                flags |= 1 << bit
                out += struct.pack("<H", ((off - 1) << 4) | (ln - MIN_MATCH))
                i += ln
            else:
                out += bytes([src[i]])
                i += 1
        out[fpos] = flags
    return bytes(out)


def _find_match(src, i, n):
    limit = min(MAX_MATCH, n - i)
    if limit < MIN_MATCH:
        return 0, 0
    start = i - WINDOW
    if start < 0:
        start = 0
    best_len = 0
    best_off = 0
    j = i - 1
    while j >= start:
        l = 0
        while l < limit and src[j + l] == src[i + l]:
            l += 1
        if l > best_len:
            best_len = l
            best_off = i - j
            if best_len == limit:
                break
        j -= 1
    if best_len < MIN_MATCH:
        return 0, 0
    return best_off, best_len


def decompress(src):
    magic, total = struct.unpack_from("<II", src, 0)
    if magic != LZSS_MAGIC:
        raise ValueError("bad lzss magic")
    out = bytearray()
    i = 8
    n = len(src)
    while len(out) < total:
        if i >= n:
            raise ValueError("truncated lzss stream")
        flags = src[i]
        i += 1
        for bit in range(8):
            if len(out) >= total:
                break
            if flags & (1 << bit):
                tok = struct.unpack_from("<H", src, i)[0]
                i += 2
                off = (tok >> 4) + 1
                ln = (tok & 0xF) + MIN_MATCH
                start = len(out) - off
                for k in range(ln):
                    out.append(out[start + k])
            else:
                out.append(src[i])
                i += 1
    return bytes(out)
