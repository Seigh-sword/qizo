#!/usr/bin/env bash
set -uo pipefail

cd "$(dirname "$0")/../.."

image=${1:?usage: tools/ci/qemuboot.sh <image> [tag]}
tag=${2:-console}
timeout_s=${QIZO_BOOT_TIMEOUT:-75}
expect=${QIZO_BOOT_EXPECT:-shell ready}
status=".qizo-boot-$tag.status"

if command -v qemu-system-x86_64 >/dev/null 2>&1; then
	qemu=qemu-system-x86_64
elif command -v apt-get >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
	echo "qizo: installing qemu-system-x86"
	sudo -n apt-get update -qq >/dev/null 2>&1 || true
	sudo -n apt-get install -y -qq --no-install-recommends qemu-system-x86 >/dev/null 2>&1 || true
	qemu=qemu-system-x86_64
	command -v "$qemu" >/dev/null 2>&1 || qemu=""
else
	qemu=""
fi

if [ -z "$qemu" ]; then
	echo "skip" >"$status"
	echo "qizo: qemu-system-x86_64 is not available, boot test skipped" >&2
	exit 2
fi

log="serial-$tag.log"
: >"$log"

drive=()
case "$image" in
	*.iso) drive=(-cdrom "$image" -boot d) ;;
	*) drive=(-drive "file=$image,format=raw,if=ide" -boot c) ;;
esac

timeout "$timeout_s" "$qemu" \
	-machine pc -m 64 -smp 1 \
	-display none -monitor none -serial "file:$log" \
	-no-reboot -no-shutdown \
	"${drive[@]}" >/dev/null 2>&1 &
pid=$!

for _ in $(seq 1 "$timeout_s"); do
	if grep -qa "$expect" "$log" 2>/dev/null; then
		break
	fi
	sleep 1
done

kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

if ! [ -s "$log" ]; then
	echo "fail" >"$status"
	echo "qizo: $image produced no serial output at all" >&2
	exit 1
fi

cat "$log"

if grep -qa 'panic' "$log"; then
	echo "fail" >"$status"
	echo "qizo: kernel panic while booting $image" >&2
	exit 1
fi

if ! grep -qa "$expect" "$log"; then
	echo "fail" >"$status"
	echo "qizo: $image never printed '$expect'" >&2
	exit 1
fi

echo "ok" >"$status"
printf 'qizo: %s booted to the shell, %s serial lines, marker "%s" found\n' \
	"$image" "$(wc -l <"$log" | tr -d ' ')" "$expect"
exit 0
