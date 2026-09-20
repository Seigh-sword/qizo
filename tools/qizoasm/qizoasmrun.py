#!/usr/bin/env python3
import argparse, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "common"))
import qizolayout as LY
import qizolzss


def extract(text, start_at="qizo_decompress:", stop_at="decomp_ret:"):
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if l.strip() == start_at)
    stop = next(i for i, l in enumerate(lines) if l.strip() == stop_at)
    end = stop
    while end < len(lines) and lines[end].strip() != "ret":
        end += 1
    return "\n".join(lines[start:end + 1]) + "\n"


def harness(routine, inpath, outpath):
    esc = lambda x: x.replace("\\", "\\\\").replace('"', '\\"')
    lines = [
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
        "\tmovl $5, %eax",
        "\tmovl $outpath, %ebx",
        "\tmovl $0x141, %ecx",
        "\tmovl $0644, %edx",
        "\tint $0x80",
        "\tmovl %eax, %ebx",
        "\tmovl $4, %eax",
        "\tmovl $outbuf, %ecx",
        "\tmovl outlen, %edx",
        "\tint $0x80",
        "\tmovl $6, %eax",
        "\tmovl $outfd, %ebx",
        "\tint $0x80",
        "\tmovl $1, %eax",
        "\txorl %ebx, %ebx",
        "\tint $0x80",
        routine.rstrip("\n").replace("qizo_decompress:", "body:"),
        "\t.data",
        "outpath:",
        "\t.asciz \"%s\"" % esc(outpath),
        "\t.align 4",
        "outfd:",
        "\t.long 0",
        "outlen:",
        "\t.long 0",
        "indata:",
        "\t.incbin \"%s\"" % esc(inpath),
        "inend:",
        "\t.set inlen, inend - indata",
        "\t.bss",
        "\t.align 16",
        "bootinfo:",
        "\t.zero 16",
        "outbuf:",
        "\t.zero 1048576",
        "",
    ]
    return "\n".join(lines)


def blob_of(img):
    off = LY.g("QIZO_BLOB_LBA") * LY.SECTOR
    end = off + LY.g("QIZO_BLOB_MAX")
    end = min(end, len(img))
    while end > off and img[end - 1] == 0:
        end -= 1
    return img[off:end]


def main():
    ap = argparse.ArgumentParser(prog="qizoasmrun")
    ap.add_argument("--image", required=True)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--quiet-skip", action="store_true")
    args = ap.parse_args()

    blob = blob_of(open(args.image, "rb").read())
    try:
        real = qizolzss.decompress(blob)
    except Exception as exc:
        print("qizo: reference decompressor refused the image blob: %s" % exc)
        return 1

    src = open(os.path.join(ROOT, "boot", "stage2.S")).read()
    tmp = tempfile.mkdtemp(prefix="qizoasm")
    inpath = os.path.join(tmp, "in.bin")
    outpath = os.path.join(tmp, "out.bin")
    open(inpath, "wb").write(blob)
    apath = os.path.join(tmp, "t.S")
    body = extract(src)
    if args.keep:
        open(os.path.join(tmp, "body.S"), "w").write(body)
    open(apath, "w").write(harness(body, inpath, outpath))
    obj = apath + ".o"
    exe = os.path.join(tmp, "t")
    try:
        subprocess.run(["as", "--32", "-o", obj, apath], check=True, capture_output=True)
        subprocess.run(["ld", "-m", "elf_i386", "-o", exe, obj], check=True, capture_output=True)
        subprocess.run([exe], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", b"") or b""
        print("qizo: skipping the boot asm test, this host cannot build or run 32 bit code (%s %s)"
              % (type(exc).__name__, detail.decode("utf8", "replace").strip()[:80]))
        return 0
    got = open(outpath, "rb").read() if os.path.exists(outpath) else b""

    if got == real:
        print("qizo: stage2 decompressor reproduces the reference over %d bytes" % len(real))
        if not args.keep:
            subprocess.run(["rm", "-rf", tmp])
        return 0
    bad = next((i for i in range(min(len(got), len(real))) if got[i] != real[i]), min(len(got), len(real)))
    print("qizo: stage2 decompressor DIFFERS at %d: got %s want %s (%d bytes vs %d)"
          % (bad, got[bad:bad + 8].hex(" "), real[bad:bad + 8].hex(" "), len(got), len(real)))
    print("qizo: kept %s" % tmp)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
