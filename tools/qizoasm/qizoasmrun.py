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


def span(text, stop_at, start_at=None):
    lines = text.split("\n")
    start = 0 if start_at is None else next(i for i, l in enumerate(lines) if l.strip() == start_at)
    stop = next(i for i, l in enumerate(lines) if l.strip() == stop_at)
    return "\n".join(lines[start:stop]) + "\n"


def assemble_run64(tmp, name, asm, objcopy=False):
    src = os.path.join(tmp, name + ".S")
    open(src, "w").write(asm)
    obj = src + ".o"
    exe = os.path.join(tmp, name)
    try:
        subprocess.run(["as", "-o", obj, src], check=True, capture_output=True)
        subprocess.run(["ld", "-o", exe, obj], check=True, capture_output=True)
        subprocess.run([exe], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise HostUnsupported(exc)
    out = subprocess.run(["nm", exe], check=True, capture_output=True)
    syms = {}
    for line in out.stdout.decode("utf8", "replace").split("\n"):
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def spill64(fd_slot, buf, size):
    return [
        "\tleaq apath(%rip), %rdi",
        "\tmovl $2, %eax",
        "\tmovl $0x142, %esi",
        "\tmovl $0644, %edx",
        "\tsyscall",
        "\tmovq %rax, " + fd_slot,
        "\tmovl $1, %eax",
        "\tmovq " + fd_slot + ", %rdi",
        "\tleaq " + buf + "(%rip), %rsi",
        "\tmovl $" + str(size) + ", %edx",
        "\tsyscall",
        "\tmovl $3, %eax",
        "\tmovq " + fd_slot + ", %rdi",
        "\tsyscall",
    ]


def idt_case(tmp):
    text = open(os.path.join(ROOT, "kernel", "idt.S")).read()
    stubs = span(text, "qizo_isr_common:")
    install = span(text, ".size qizo_idt_install, . - qizo_idt_install",
                   start_at="qizo_idt_install:")
    install = install.replace("lidt (%rax)", "nop")
    dump = os.path.join(tmp, "idt.bin")
    asm = "\n".join([
        "\t.code64",
        "\t.text",
        "\t.globl _start",
        "_start:",
        "\tcall body",
    ] + spill64("fdslot", "qizo_idt", 256 * 16) + [
        "\tmovl $60, %eax",
        "\txorl %edi, %edi",
        "\tsyscall",
        stubs.rstrip("\n"),
        "qizo_isr_common:",
        "\tret",
        install.rstrip("\n").replace("qizo_idt_install:", "body:"),
        "\t.data",
        "apath:",
        "\t.asciz \"%s\"" % dump.replace("\\", "\\\\"),
        "\t.align 8",
        "fdslot:",
        "\t.quad 0",
        "qizo_idt_ptr:",
        "\t.word 256 * 16 - 1",
        "\t.quad qizo_idt",
        "\t.bss",
        "\t.balign 16",
        "qizo_idt:",
        "\t.skip 4096",
        "",
    ])
    open(os.path.join(tmp, "idt.S.src"), "w").write(asm)
    syms = assemble_run64(tmp, "idt", asm)
    data = open(dump, "rb").read()
    if len(data) < 4096:
        return ["idt dump truncated at %d bytes" % len(data)]
    bad = []
    for vec in range(256):
        gate = data[vec * 16:vec * 16 + 16]
        off_lo, sel, attr_w, off_mid, off_hi, rsv = struct.unpack_from("<HHHHII", gate, 0)
        attr = (attr_w >> 8) & 0xFF
        ist = attr_w & 0x0F
        off = off_lo | off_mid << 16 | off_hi << 32
        want_name = "qizo_stub_%d" % vec if vec < 32 else "qizo_irq_%d" % vec
        want = syms.get(want_name)
        if want is None:
            bad.append("gate %d: no stub symbol %s" % (vec, want_name))
            break
        if off != want:
            bad.append("gate %d points at 0x%x, its stub is at 0x%x" % (vec, off, want))
            break
        if sel != 0x08:
            bad.append("gate %d loads selector 0x%x instead of the 64 bit code segment" % (vec, sel))
            break
        if attr != 0x8E or ist:
            bad.append("gate %d has attributes %02x ist %d, expected 8e ist 0" % (vec, attr, ist))
            break
        if rsv:
            bad.append("gate %d has a non zero reserved word" % vec)
            break
    return bad



def fault_report_case(tmp):
    src = open(os.path.join(ROOT, "boot", "stage2.S")).read()
    body = src[src.index(".section .qizo_fault"):].replace(".section .qizo_fault,\"ax\"", "")
    body = body.replace("\tinb %dx, %al", "\tmovl $0x60, %eax")
    body = body.replace("\toutb %al, %dx", "\tmovb %al, (%rbx)\n\tincq %rbx")
    body = body.replace("\tmovq %cr2, %r9", "\tmovq $0xdeadbeef, %r9")
    body = body.replace("\tcli\n", "\tnop\n")
    body = body.replace("\tmovq %cr2, %rdx", "\tmovq $0xdeadbeef, %rdx")
    tail = body.index("25:")
    nxt = body.index("\n\n", tail)
    body = body[:tail] + "\tjmp done" + body[nxt:]
    dump = os.path.join(tmp, "report.txt")
    asm = "\n".join([
        "\t.code64",
        "\t.text",
        "\t.globl _start",
        "_start:",
        "\t.set QIZO_IDT_BOOT, 0x307000",
        "\tmovabsq $outbuf, %rbx",
        "\tpushq $done",
        "\tpushq $0x2",
        "\tpushq $0x10",
        "\tpushq $0x100040",
        "\tpushq $13",
        "\tjmp qizo_fault_common",
        "done:",
        "\tleaq outbuf(%rip), %rsi",
    ] + spill64("fdslot", "outbuf", 64) + [
        "\tmovl $60, %eax",
        "\txorl %edi, %edi",
        "\tsyscall",
        body,
        "\t.data",
        "apath:",
        "\t.asciz \"%s\"" % dump.replace("\\", "\\\\"),
        "\t.align 8",
        "fdslot:",
        "\t.quad 0",
        "\t.bss",
        "\t.balign 16",
        "outbuf:",
        "\t.zero 512",
        "",
    ])
    assemble_run64(tmp, "report", asm)
    got = open(dump, "rb").read()
    want = b"F000000000000000D000000000010004000000000DEADBEEF"
    if got.startswith(want):
        return []
    return ["report printed %r, expected %r..." % (got[:len(want)], want)]


def fault_gates_case(tmp):
    src = open(os.path.join(ROOT, "boot", "stage2.S")).read()
    body = src[src.index("\tmovl $qizo_fstub_table, %esi"):src.index("\tlidt qizo_fidt_ptr")]
    dump = os.path.join(tmp, "fidt.bin")
    fake = ["\t.long 0x20000 + %d" % (i * 16) for i in range(32)]
    asm = "\n".join([
        "\t.code32",
        "\t.set QIZO_IDT_BOOT, idtblob",
        "\t.text",
        "\t.globl _start",
        "_start:",
        "\tnop",
        body.replace("lidt qizo_fidt_ptr", "nop"),
    ] + spill("fdslot", "idtblob", "movl $512, %edx") + [
        "\tmovl $1, %eax",
        "\txorl %ebx, %ebx",
        "\tint $0x80",
        "\t.data",
        "apath:",
        "\t.asciz \"%s\"" % dump.replace("\\", "\\\\"),
        "\t.align 4",
        "fdslot:",
        "\t.long 0",
        "qizo_fstub_table:",
        "\t" + "\n\t".join(fake),
        "qizo_fidt_ptr:",
        "\t.word 32 * 16 - 1",
        "\t.long QIZO_IDT_BOOT",
        "\t.bss",
        "\t.balign 16",
        "idtblob:",
        "\t.zero 512",
        "",
    ])
    assemble_run(tmp, "fidt", asm)
    data = open(dump, "rb").read()
    bad = []
    if len(data) < 512:
        return ["idt dump truncated at %d bytes" % len(data)]
    for vec in range(32):
        lo, mid = struct.unpack_from("<II", data, vec * 16)
        want_off = 0x20000 + vec * 16
        if (lo & 0xFFFF) != (want_off & 0xFFFF) or (lo >> 16) != 0x0008:
            bad.append("gate %d first word %08x, expected offset %04x with selector 0x0008"
                       % (vec, lo, want_off & 0xFFFF))
            break
        if (mid >> 16) != (want_off >> 16) or (mid & 0xFFFF) != 0x8E00:
            bad.append("gate %d second word %08x, expected offset %04x with attributes 8e00"
                       % (vec, mid, want_off >> 16))
            break
        if struct.unpack_from("<II", data, vec * 16 + 8) != (0, 0):
            bad.append("gate %d has a non zero high half" % vec)
            break
    return bad


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
    except HostUnsupported:
        print("qizo: skipping the boot asm test, this host cannot build or run 32 bit code")
        return 0
    try:
        problems = idt_case(tmp)
        if problems:
            for line in problems:
                print("qizo: kernel idt: %s" % line)
            print("qizo: kept %s" % tmp)
            return 1
        print("qizo: kernel idt gates all point at their own stub, selector and type correct")
        for label, case in (("report", fault_report_case), ("gate build", fault_gates_case)):
            problems = case(tmp)
            if problems:
                for line in problems:
                    print("qizo: boot fault %s: %s" % (label, line))
                print("qizo: kept %s" % tmp)
                return 1
            print("qizo: boot fault %s verified" % label)
    except HostUnsupported as exc:
        detail = getattr(exc.args[0], "stderr", b"") or b""
        print("qizo: skipping the idt asm test, this host cannot build or run 64 bit harness code (%s %s)"
              % (type(exc).__name__, detail.decode("utf8", "replace").strip()[:80]))
        return 0
    if not args.keep:
        subprocess.run(["rm", "-rf", tmp])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
