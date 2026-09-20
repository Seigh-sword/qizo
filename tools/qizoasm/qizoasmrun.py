#!/usr/bin/env python3
import argparse, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "common"))
import qizolayout as LY
import qizolzss

PRESENT = 0x01
WRITABLE = 0x02
PS = 0x80


class HostUnsupported(Exception):
    pass


def extract(text, start_at, stop_at, to_ret=True):
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if l.strip() == start_at)
    stop = next(i for i, l in enumerate(lines) if l.strip() == stop_at)
    end = stop
    if to_ret:
        while end < len(lines) and lines[end].strip() != "ret":
            end += 1
        end += 1
    return "\n".join(lines[start:end]) + "\n"


def spill(fd_slot, buf, edx_line):
    return [
        "\tmovl $5, %eax",
        "\tmovl $apath, %ebx",
        "\tmovl $0x141, %ecx",
        "\tmovl $0644, %edx",
        "\tint $0x80",
        "\tmovl %eax, " + fd_slot,
        "\tmovl $4, %eax",
        "\tmovl " + fd_slot + ", %ebx",
        "\tmovl $" + buf + ", %ecx",
        "\t" + edx_line,
        "\tint $0x80",
        "\tmovl $6, %eax",
        "\tmovl " + fd_slot + ", %ebx",
        "\tint $0x80",
    ]


def epilogue():
    return ["\tmovl $1, %eax", "\txorl %ebx, %ebx", "\tint $0x80"]


def assemble_run(tmp, name, asm):
    src = os.path.join(tmp, name + ".S")
    open(src, "w").write(asm)
    obj = src + ".o"
    exe = os.path.join(tmp, name)
    try:
        subprocess.run(["as", "--32", "-o", obj, src], check=True, capture_output=True)
        subprocess.run(["ld", "-m", "elf_i386", "-o", exe, obj], check=True, capture_output=True)
        subprocess.run([exe], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise HostUnsupported(exc)


def blob_of(img):
    off = LY.g("QIZO_BLOB_LBA") * LY.SECTOR
    end = min(off + LY.g("QIZO_BLOB_MAX"), len(img))
    while end > off and img[end - 1] == 0:
        end -= 1
    return img[off:end]


def decompress_case(src, tmp, blob):
    outpath = os.path.join(tmp, "out.bin")
    inpath = os.path.join(tmp, "in.bin")
    open(inpath, "wb").write(blob)
    body = extract(src, "qizo_decompress:", "decomp_ret:", to_ret=True)
    asm = "\n".join([
        "\t.code32",
        "\t.set QIZO_LZSS_MAGIC, %d" % LY.g("QIZO_LZSS_MAGIC"),
        "\t.set QIZO_BOOTINFO, bootinfo",
        "\t.set QIZO_BI_ERR, 0",
        "\t.text",
        "\t.globl _start",
        "_start:",
        "\tmovl $outbuf, %edi",
        "\tmovl $indata, %esi",
        "\tmovl $inlen, %ecx",
        "\tcall body",
        "\tmovl indata + 4, %eax",
        "\tmovl %eax, outlen",
    ] + spill("outfd", "outbuf", "movl outlen, %edx") + epilogue() + [
        body.rstrip("\n").replace("qizo_decompress:", "body:"),
        "\t.data",
        "apath:",
        "\t.asciz \"%s\"" % outpath.replace("\\", "\\\\"),
        "\t.align 4",
        "outfd:",
        "\t.long 0",
        "outlen:",
        "\t.long 0",
        "indata:",
        "\t.incbin \"%s\"" % inpath.replace("\\", "\\\\"),
        "inend:",
        "\t.set inlen, inend - indata",
        "\t.bss",
        "\t.align 16",
        "bootinfo:",
        "\t.zero 16",
        "outbuf:",
        "\t.zero 1048576",
        "",
    ])
    open(outpath, "wb").close()
    return asm, outpath


def pages_case(src, tmp):
    dump = os.path.join(tmp, "tables.bin")
    body = extract(src, "qizo_zero:", "fail_c1:", to_ret=False)
    body = body.replace("qizo_build_pages:", "body:")
    sets = [
        "QIZO_PAGE_PML4, tables",
        "QIZO_PAGE_PDPT, tables + 0x1000",
        "QIZO_PAGE_PDT, tables + 0x2000",
        "QIZO_PT_PAGE, %d" % LY.g("QIZO_PT_PAGE"),
        "QIZO_PT_DIR, %d" % LY.g("QIZO_PT_DIR"),
    ]
    asm = "\n".join(["\t.code32"] + ["\t.set " + x for x in sets] + [
        "\t.text",
        "\t.globl _start",
        "_start:",
        "\tcall body",
    ] + spill("fdslot", "tables", "movl $0x6000, %edx") + epilogue() + [
        body.rstrip("\n"),
        "\t.data",
        "apath:",
        "\t.asciz \"%s\"" % dump.replace("\\", "\\\\"),
        "\t.align 4",
        "fdslot:",
        "\t.long 0",
        "\t.bss",
        "\t.align 4096",
        "tables:",
        "\t.zero 0x6000",
        "",
    ])
    assemble_run(tmp, "pages", asm)
    tables = open(dump, "rb").read()
    if len(tables) < 0x6000:
        return ["page table dump truncated at %d bytes" % len(tables)]
    return check_tables(tables)


def check_tables(tables):
    bad = []

    def ent(page, idx):
        off = page * 0x1000 + idx * 8
        return struct.unpack_from("<II", tables, off)

    base = (ent(0, 0)[0] & ~0xFFF) - 0x1000

    def dir_entry(where, page, idx, want):
        lo, hi = ent(page, idx)
        if not (lo & PRESENT):
            bad.append("%s not present" % where)
        if lo & PS:
            bad.append("%s sets the page size bit in a non leaf entry" % where)
        if lo & 0x300:
            bad.append("%s sets reserved flag bits in a non leaf entry" % where)
        if hi:
            bad.append("%s points above 4 GiB" % where)
        if (lo & ~0xFFF) - base != want:
            bad.append("%s points at offset 0x%x in the table block, expected 0x%x"
                       % (where, (lo & ~0xFFF) - base, want))

    dir_entry("PML4[0]", 0, 0, 0x1000)
    for idx in range(1, 512):
        lo, hi = ent(0, idx)
        if lo or hi:
            bad.append("PML4[%d] is set but nothing maps that range" % idx)
            break
    for idx in range(4):
        dir_entry("PDPT[%d]" % idx, 1, idx, 0x2000 + idx * 0x1000)
    for idx in range(4, 512):
        lo, hi = ent(1, idx)
        if lo or hi:
            bad.append("PDPT[%d] maps past 4 GiB without a table" % idx)
            break
    for slot in range(2048):
        lo, hi = ent(2 + (slot * 8) // 0x1000, (slot * 8 % 0x1000) // 8)
        want = slot * 0x200000 | LY.g("QIZO_PT_PAGE")
        if lo != want or hi:
            bad.append("PDT entry %d is %08x %08x, expected identity map at %08x %08x"
                       % (slot, lo, hi, want & 0xFFFFFFFF, want >> 32))
            break
    for la in (0x4000, 0x10000, 0x44000, 0x100000, 0x126000, 0x180000, 0x300000, 0x305FF8,
               0xFFE00000):
        slot = ((la >> 21) & 0x1FF) | (((la >> 30) & 3) << 9)
        page = 2 + slot // 512
        lo, hi = ent(page, slot % 512)
        if not (lo & PRESENT) or (lo & ~0xFFF) != (la & ~0x1FFFFF):
            bad.append("linear 0x%x has no identity mapping" % la)
    return bad


def main():
    ap = argparse.ArgumentParser(prog="qizoasmrun")
    ap.add_argument("--image", required=True)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    img = open(args.image, "rb").read()
    blob = blob_of(img)
    try:
        real = qizolzss.decompress(blob)
    except Exception as exc:
        print("qizo: reference decompressor refused the image blob: %s" % exc)
        return 1

    src = open(os.path.join(ROOT, "boot", "stage2.S")).read()
    tmp = tempfile.mkdtemp(prefix="qizoasm")
    try:
        outpath = os.path.join(tmp, "out.bin")
        inpath = os.path.join(tmp, "in.bin")
        open(inpath, "wb").write(blob)
        asm, _ = decompress_case(src, tmp, blob)
        assemble_run(tmp, "decomp", asm)
        got = open(outpath, "rb").read() if os.path.exists(outpath) else b""
        if got != real:
            bad = next((i for i in range(min(len(got), len(real)))
                        if got[i] != real[i]), min(len(got), len(real)))
            print("qizo: stage2 decompressor DIFFERS at %d: got %s want %s (%d bytes vs %d)"
                  % (bad, got[bad:bad + 8].hex(" "), real[bad:bad + 8].hex(" "),
                     len(got), len(real)))
            print("qizo: kept %s" % tmp)
            return 1
        print("qizo: stage2 decompressor reproduces the reference over %d bytes" % len(real))

        problems = pages_case(src, tmp)
        if problems:
            for line in problems:
                print("qizo: stage2 page tables: %s" % line)
            print("qizo: kept %s" % tmp)
            return 1
        print("qizo: stage2 page tables identity map 4 GiB with 2 MiB pages, no reserved bits set")
    except HostUnsupported as exc:
        detail = getattr(exc.args[0], "stderr", b"") or b""
        print("qizo: skipping the boot asm test, this host cannot build or run 32 bit code (%s %s)"
              % (type(exc).__name__, detail.decode("utf8", "replace").strip()[:80]))
        return 0
    if not args.keep:
        subprocess.run(["rm", "-rf", tmp])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
