import os
import subprocess
import sys


def run(cmd, env=None, cwd=None, capture=False):
    out = None
    if capture:
        res = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True)
        out = res.stdout
    else:
        res = subprocess.run(cmd, env=env, cwd=cwd)
    if res.returncode:
        sys.stderr.write("qizo: command failed: " + " ".join(cmd) + "\n")
        if out:
            sys.stderr.write(out)
        raise SystemExit(2)
    return out


def nm(elfpath):
    text = run(["nm", "-n", elfpath], capture=True)
    table = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            value = int(parts[0], 16)
        except ValueError:
            continue
        table[parts[-1]] = value
    return table


def size_bytes(path):
    return os.path.getsize(path)


def human(n):
    units = ["B", "KiB", "MiB", "GiB"]
    i = 0
    v = float(n)
    while v >= 1024.0 and i < len(units) - 1:
        v /= 1024.0
        i += 1
    if i == 0:
        return "%d B" % n
    return "%.1f %s" % (v, units[i])


def percent(a, b):
    if b == 0:
        return "0%"
    return "%.1f%%" % (100.0 * a / b)


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def read(path):
    with open(path, "rb") as f:
        return f.read()
