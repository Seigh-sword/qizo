#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

status=0

while IFS= read -r file; do
	if grep -qaP '\x00' "$file" 2>/dev/null; then
		echo "qizo: binary blob committed: $file" >&2
		status=1
	fi
done < <(git ls-files)

if [ "$status" -eq 0 ]; then
	echo "qizo: no binary blobs in the repository"
fi

tabs=$(git grep -lP '\t$' -- '*.c' '*.h' '*.S' '*.py' '*.sh' '*.mk' Makefile 2>/dev/null || true)
if [ -n "$tabs" ]; then
	echo "qizo: trailing whitespace in:" >&2
	echo "$tabs" >&2
	status=1
fi

emoji=$(git grep -lP '[\x{1F300}-\x{1FAFF}\x{2600}-\x{27BF}]' -- . 2>/dev/null || true)
if [ -n "$emoji" ]; then
	echo "qizo: emoji found in:" >&2
	echo "$emoji" >&2
	status=1
fi

pyfiles=$(git ls-files 'tools/*.py' | tr '\n' ' ')
if [ -n "${pyfiles// /}" ]; then
	python3 -m py_compile $pyfiles
	echo "qizo: python tools compile clean"
fi

if grep -rn --include=*.c --include=*.h --include=*.S '/\*' kernel boot | grep -v qizo_font.h >/dev/null 2>&1; then
	echo "qizo: comments found in kernel or boot sources" >&2
	status=1
fi

exit "$status"
