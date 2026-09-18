import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))

import qizolayout as LY

def human(n):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return "%.1f %s" % (n, unit) if unit != "B" else "%d B" % n
        n /= 1024.0


def size(path):
    return os.path.getsize(path) if os.path.exists(path) else -1


def main():
    art = "build/artifacts"
    checks = [
        ("qizo.img", size(os.path.join(art, "qizo.img")), 4 << 20),
        ("qizo.img.xz", size(os.path.join(art, "qizo.img.xz")), 64 << 10),
        ("qizo.iso", size(os.path.join(art, "qizo.iso")), 4 << 20),
        ("qizo.iso.xz", size(os.path.join(art, "qizo.iso.xz")), 64 << 10),
        ("kernel flat image", size("build/qizo.bin"), LY.KERNEL_MAX),
        ("stage1", size("build/boot/stage1.bin"), 512),
        ("stage2", size("build/boot/stage2.bin"), LY.STAGE2_BYTES),
    ]
    bad = []
    for name, got, limit in checks:
        if got < 0:
            bad.append("%s missing" % name)
            print("%-18s missing   budget %s" % (name, human(limit)))
            continue
        state = "ok" if got <= limit else "OVER"
        if got > limit:
            bad.append("%s is %s, budget %s" % (name, human(got), human(limit)))
        print("%-18s %-9s budget %-9s %s" % (name, human(got), human(limit), state))
    if os.path.exists("build/qizo.elf"):
        import subprocess
        out = subprocess.run(["size", "-A", "build/qizo.elf"], capture_output=True, text=True).stdout
        print()
        print(out.strip())
    if bad:
        print()
        print("qizo: bloat check failed:")
        for line in bad:
            print("  " + line)
        return 1
    print()
    print("qizo: inside every size budget")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
