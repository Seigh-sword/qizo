#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

need=(gcc as ld objcopy nm size make python3 xz)
missing=()
for tool in "${need[@]}"; do
	command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if [ ${#missing[@]} -ne 0 ]; then
	echo "qizo: missing tools: ${missing[*]}" >&2
	exit 1
fi

if [ ! -x .venv/bin/python ]; then
	python3 -m venv .venv
fi

. .venv/bin/activate
python -c "
import sys
print('qizo: python %s' % sys.version.split()[0])
for mod in ('struct', 'zlib', 'argparse', 'os'):
    __import__(mod)
print('qizo: stdlib ok')
"

echo "qizo: toolchain ready"
gcc --version | head -1
make --version | head -1
ld --version | head -1
deactivate
