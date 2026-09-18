#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if [ ! -f build/artifacts/qizo.img ] || [ ! -f build/artifacts/qizo.iso ]; then
	echo "qizo: run tools/ci/build.sh first, the artifacts are missing" >&2
	exit 1
fi

version=$(tr -d '[:space:]' < VERSION 2>/dev/null || echo dev)
commit=$(git rev-parse --short HEAD)
stamp=$(date -u +%Y%m%dT%H%M%SZ)

rm -rf dist
mkdir -p dist

cp build/artifacts/qizo.img "dist/qizo-$version.img"
cp build/artifacts/qizo.iso "dist/qizo-$version.iso"
cp build/artifacts/qizo.psf1 "dist/qizo-$version.psf1"
cp build/qizo.elf "dist/qizo-$version-kernel.elf"
xz -9kc -T0 build/artifacts/qizo.img > "dist/qizo-$version.img.xz"
xz -9kc -T0 build/artifacts/qizo.iso > "dist/qizo-$version.iso.xz"
gzip -9nc build/artifacts/qizo.img > "dist/qizo-$version.img.gz"

for log in serial-disk.log serial-iso.log serial-console.log; do
	[ -f "$log" ] && cp "$log" "dist/qizo-$version-${log#serial-}"
done

rows=""
for f in dist/qizo-*; do
	size=$(stat -c %s "$f")
	name=$(basename "$f")
	rows="$rows| \`$name\` | $size bytes |"$'\n'
done

{
	echo "qizo $version, commit \`$commit\`, built $stamp."
	echo
	echo "Write the raw image to a stick or a disk with \`dd\`, or boot it in qemu:"
	echo
	echo '```sh'
	echo "qemu-system-x86_64 -m 64 -drive file=qizo-$version.img,format=raw,if=ide \\"
	echo "    -display none -serial stdio -monitor none"
	echo '```'
	echo
	echo "| file | size |"
	echo "|---|---|"
	printf '%s' "$rows"
	echo
	echo 'The `.img` and `.iso` are the same disk: the ISO wraps the image as an'
	echo 'El Torito hard disk emulation payload, so either one boots.'
	echo
	echo 'QEMU boot check:'
	for tag in disk iso console; do
		[ -f ".qizo-boot-$tag.status" ] || continue
		echo "- $tag: $(cat ".qizo-boot-$tag.status")"
	done
	if [ ! -f .qizo-boot-disk.status ] && [ ! -f .qizo-boot-iso.status ]; then
		echo '- not run on this build'
	fi
} > dist/NOTES.md

(
	cd dist
	find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%P\n' | sort |
		xargs -r sha256sum > SHA256SUMS
)

echo "qizo: release notes and checksums"
cat dist/SHA256SUMS
