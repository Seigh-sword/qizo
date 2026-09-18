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

probe() {
	if grep -qa -- "$2" "$log" 2>/dev/null; then
		printf '%s ' "$1"
	fi
}

report() {
	local sev="$1" seen masks pat m
	seen=""
	masks="stage1 stage2 pm e820 longmm jump6 con mem irq cal timer rep shell prompt panic"
	for m in $masks; do
		pat="$m"
		case "$m" in
			stage1) pat="1:" ;;
			stage2) pat="2:" ;;
			pm) pat="P" ;;
			e820) pat="M" ;;
			longmm) pat="L" ;;
			jump6) pat="6" ;;
			con) pat="console up" ;;
			mem) pat="memory up" ;;
			irq) pat="irqs live" ;;
			cal) pat="calibrating" ;;
			timer) pat="timer" ;;
			rep) pat="reporting" ;;
			shell) pat="shell" ;;
			prompt) pat="qizo>" ;;
			panic) pat="panic" ;;
		esac
		if grep -qa -- "$pat" "$log" 2>/dev/null; then
			seen="$seen$m "
		fi
	done
	{
		printf 'qizo %s: reached[%s] bytes=%s trace=%s' "$tag" "${seen:-none}" "${bytes:-0}" "${tlen:-0}"
		if [ -f "$err" ] && grep -qaE 'error|failed|unsupported' "$err" 2>/dev/null; then
			printf ' qemuerr'
		fi
		printf '\n'
	} >".qizo-boot-$tag.probe"
	probe_sev=warning
	if [ "$sev" = failure ]; then
		probe_sev=error
	fi
	printf '::%s::qizo %s: %s\n' "$probe_sev" "$tag" "$(cat ".qizo-boot-$tag.probe" | clean)"
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
tlen=$(wc -l <"$trace" 2>/dev/null | tr -d ' ')
tlen=${tlen:-0}

if [ "$bytes" -eq 0 ]; then
	echo "fail" >"$status"
	report "failure"
	fail "no serial output at all from $image, qemu said: $(tail -2 "$err" 2>/dev/null)"
	exit 1
fi

sed -e 's/[^[:print:]]/./g' "$log" | tail -40

if grep -qa 'panic' "$log"; then
	echo "fail" >"$status"
	report "failure"
	fail "kernel panic: $(grep -a 'panic' "$log" | head -2)"
	exit 1
fi

if ! grep -qa "$expect" "$log"; then
	echo "fail" >"$status"
	report "failure"
	fail "stopped before the shell, waiting for: $expect"
	exit 1
fi

echo "ok" >"$status"
note "$bytes serial bytes, booted to the shell"
printf 'qizo: %s booted, %s serial bytes, marker "%s" found\n' \
	"$image" "$bytes" "$expect"
exit 0
