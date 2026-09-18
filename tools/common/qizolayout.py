import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INC = os.path.join(ROOT, "boot", "qizoboot.inc")

_pat = re.compile(r"^\s*\.set\s+(\w+)\s*,\s*(.+?)\s*$")


def parse(path=INC):
    vals = {}
    for line in open(path):
        m = _pat.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        try:
            vals[name] = int(expr, 0)
        except ValueError:
            try:
                vals[name] = eval(expr, dict(vals))
            except Exception:
                raise SystemExit("qizo: cannot evaluate layout symbol %s" % name)
    return vals


LAYOUT = parse()


def g(name):
    return LAYOUT[name]


SECTOR = 512
STAGE1_ARGS_OFF = LAYOUT["QIZO_BIOS_ARG"] - 0x7C00
QIZOK_MAGIC = LAYOUT["QIZO_QIZOK_MAGIC"]
LZSS_MAGIC = LAYOUT["QIZO_LZSS_MAGIC"]
PART_LBA = LAYOUT["QIZO_PART_LBA"]
BLOB_LBA = LAYOUT["QIZO_BLOB_LBA"]
BLOB_MAX = LAYOUT["QIZO_BLOB_MAX"]
BLOB_PHYS = LAYOUT["QIZO_BLOB_PHYS"]
STAGE2 = LAYOUT["QIZO_STAGE2"]
STAGE2_BYTES = LAYOUT["QIZO_STAGE2_BYTES"]
STAGE2_SECTORS = LAYOUT["QIZO_STAGE2_SECTORS"]
KTEXT = LAYOUT["QIZO_KTEXT"]
SCRATCH = LAYOUT["QIZO_SCRATCH"]
E820_BUF = LAYOUT["QIZO_E820_BUF"]
PM_STACK = LAYOUT["QIZO_PM_STACK"]
PAGE_PML4 = LAYOUT["QIZO_PAGE_PML4"]
PAGE_PDPT = LAYOUT["QIZO_PAGE_PDPT"]
BOOTINFO = LAYOUT["QIZO_BOOTINFO"]
BOOTINFO_SIZE = LAYOUT["QIZO_BI_SIZE"]
KERNEL_MAX = LAYOUT["QIZO_KERNEL_MAX"]
E820_MAX = LAYOUT["QIZO_E820_MAX"]
E820_ENT = LAYOUT["QIZO_E820_ENT"]
QIZOK_HDR = LAYOUT["QIZO_QIZOK_HDR"]
CHUNK = 64
BI = {k: LAYOUT[k] for k in LAYOUT if k.startswith("QIZO_BI_")}
