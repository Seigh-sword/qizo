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
	local m pat vec rip hexc
	seen=""
	for m in stage1 stage2 a20on a20off pm e820 longmm jump6 con mem irq cal timer rep shell prompt panic exc; do
		pat="$m"
		case "$m" in
			stage1) pat="1:" ;;
			stage2) pat="2:" ;;
			a20on) pat="A1" ;;
			a20off) pat="A0" ;;
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
			panic) pat="cpu exception" ;;
			exc) pat="qizo: panic" ;;
		esac
		if grep -qa -- "$pat" "$log" 2>/dev/null; then
			seen="$seen$m "
		fi
	done
	probe_msg="reached[${seen:-none}] bytes=${bytes:-0} trace=${tlen:-0}"
	if [ -f "$err" ] && grep -qaE 'error|failed|unsupported' "$err" 2>/dev/null; then
		probe_msg="$probe_msg qemuerr"
	fi
	vec=$(sed -n 's/.*cpu exception \([0-9][0-9]*\).*/\1/p' "$log" 2>/dev/null | head -1)
	vec=$(printf '%s' "$vec" | tr -dc '0-9')
	if [ -n "$vec" ]; then
		probe_msg="$probe_msg vec=$vec"
	fi
	rip=$(sed -n 's/.*rip=\(0x[0-9a-fA-F][0-9a-fA-F]*\).*/\1/p' "$log" 2>/dev/null | head -1)
	rip=$(printf '%s' "$rip" | tr -dc '0-9a-f')
	if [ -n "$rip" ]; then
		probe_msg="$probe_msg rip=$rip"
	fi
	if [ -s "$log" ]; then
		hexc=$(head -c 24 "$log" | od -An -tx1 | tr -dc '0-9a-f')
		if [ -n "$hexc" ]; then
			probe_msg="$probe_msg hex=$hexc"
		fi
	fi
	printf 'qizo %s: %s\n' "$tag" "$probe_msg" >".qizo-boot-$tag.probe"
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

qver=$("$qemu" --version 2>/dev/null | head -1 | cut -c1-40)
note "qemu $qver" 

ibytes=$(wc -c <"$image" 2>/dev/null | tr -dc '0-9')
note "image $image ok, ${ibytes:-0} bytes, expect \"${expect}\""

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
qemu_rc=0
wait "$pid" 2>/dev/null || qemu_rc=$?
note "qemu exited rc=$qemu_rc after ${timeout_s}s window"

bytes=$(wc -c <"$log" 2>/dev/null | tr -d ' ')
bytes=${bytes:-0}
tlen=$(wc -l <"$trace" 2>/dev/null | tr -d ' ')
tlen=${tlen:-0}

if [ "$bytes" -eq 0 ]; then
	echo "fail" >"$status"
	report
	fail "no serial output at all, $probe_msg"
	exit 1
fi

{ printf 'qizo %s: last serial lines\n' "$tag"
  sed -e 's/[^[:print:]]/./g' "$log" | tail -12 | cut -c1-200
} >"qizo-console-$tag.txt"

if grep -qa 'panic' "$log"; then
	echo "fail" >"$status"
	report
	fail "kernel stopped: $probe_msg"
	exit 1
fi

if ! grep -qa "$expect" "$log"; then
	echo "fail" >"$status"
	report
	fail "stopped before the shell, $probe_msg"
	exit 1
fi

echo "ok" >"$status"
note "$bytes serial bytes, booted to the shell"
printf 'qizo: %s booted, %s serial bytes, marker "%s" found\n' \
	"$image" "$bytes" "$expect"
exit 0
