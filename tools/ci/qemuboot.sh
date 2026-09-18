#!/usr/bin/env bash
set -uo pipefail

cd "$(dirname "$0")/../.."

image=${1:?usage: tools/ci/qemuboot.sh <image> [tag]}
tag=${2:-console}
timeout_s=${QIZO_BOOT_TIMEOUT:-75}
expect=${QIZO_BOOT_EXPECT:-qizo> }
status=".qizo-boot-$tag.status"
log="serial-$tag.log"
err="qemu-$tag.err"
trace="qemu-$tag.trace"
bios="bios-$tag.log"

clean() {
	tr -d '\r' | tr '\n\t\0' '   ' | tr -cd ' -~' | cut -c1-700
}

note() {
	printf '::notice::qizo %s: %s\n' "$tag" "$(printf '%s' "$1" | clean)"
}

fail() {
	printf '::error::qizo %s: %s\n' "$tag" "$(printf '%s' "$1" | clean)"
}

report() {
	note "serial[$bytes]: $(head -c 300 "$log" 2>/dev/null) || tail: $(tail -c 200 "$log" 2>/dev/null) || bios: $(tail -c 200 "$bios" 2>/dev/null) || trace: $(tail -c 200 "$trace" 2>/dev/null)"
}

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
	note "qemu-system-x86_64 not available, boot test skipped"
	exit 2
fi

if [ ! -f "$image" ]; then
	echo "fail" >"$status"
	here=$(dirname "$image")
	fail "missing image $image, $here holds: $(ls -l "$here" 2>&1 | tail -6 | tr -s ' ')"
	exit 1
fi

note "qemu $("$qemu" --version | head -1)"

drive=()
case "$image" in
	*.iso) drive=(-cdrom "$image" -boot d) ;;
	*) drive=(-drive "file=$image,format=raw,if=ide" -boot c) ;;
esac

timeout "$timeout_s" "$qemu" \
	-machine pc -m 64 -smp 1 -cpu qemu64 \
	-display none -monitor none -serial "file:$log" \
	-no-reboot -no-shutdown \
	-d int,cpu_reset,guest_errors -D "$trace" \
	-debugcon "file:$bios" -global isa-debugcon.iobase=0x402 \
	"${drive[@]}" 2>"$err" &
pid=$!

for _ in $(seq 1 "$timeout_s"); do
	if grep -qa "$expect" "$log" 2>/dev/null; then
		break
	fi
	sleep 1
done

kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

bytes=$(wc -c <"$log" 2>/dev/null | tr -d ' ')
bytes=${bytes:-0}

if [ "$bytes" -eq 0 ]; then
	echo "fail" >"$status"
	report
	fail "no serial output at all from $image, qemu said: $(tail -2 "$err" 2>/dev/null)"
	exit 1
fi

cat "$log"

if grep -qa 'panic' "$log"; then
	echo "fail" >"$status"
	report
	fail "kernel panic: $(grep -a 'panic' "$log" | head -2)"
	exit 1
fi

if ! grep -qa "$expect" "$log"; then
	echo "fail" >"$status"
	report
	fail "stopped before the shell, waiting for: $expect"
	exit 1
fi

echo "ok" >"$status"
note "$bytes serial bytes, booted to the shell"
printf 'qizo: %s booted, %s serial bytes, marker "%s" found\n' \
	"$image" "$bytes" "$expect"
exit 0
