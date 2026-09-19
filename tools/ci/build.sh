#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
bash tools/ci/bootstrap.sh

. .venv/bin/activate
export PYTHON="$(pwd)/.venv/bin/python"

make -j"$(nproc 2>/dev/null || echo 2)" PYTHON="$PYTHON" all
make PYTHON="$PYTHON" test
make PYTHON="$PYTHON" check
make PYTHON="$PYTHON" size

ls -l build/artifacts
sums=(build/artifacts/qizo.img build/artifacts/qizo.iso build/artifacts/qizo.psf1)
for packed in build/artifacts/qizo.img.xz build/artifacts/qizo.iso.xz; do
	[ -f "$packed" ] && sums+=("$packed")
done
sha256sum "${sums[@]}" | tee build/artifacts/SHA256SUMS
