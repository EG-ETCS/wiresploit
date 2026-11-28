#!/usr/bin/env bash
# Pi hardware tests (level 3): the loopback self-tests, one PASS/FAIL line each.
#
#   test_pi.sh [--keep]
#
# Runs on a Raspberry Pi 5 only, with loopback jumpers between Pi header pins
# and nothing external attached. It asks you to confirm each wiring step from
# `pinctrl` output before it captures anything.
#
#   fdebugtest  stall detection works (no wiring needed)
#   bitprobe    channel map: jumper GPIO 20 -> GPIO 2, channel 0 lands in word
#               bits 0 8 16 24 in that order, and --verify sees a clean wave
#   ratesweep   10 MSa/s, 8 channels: status ok, 0 stalls
#   uart        jumper GPIO 14 -> GPIO 2: text sent on /dev/ttyAMA0 is captured
#               by pi5la and decoded by sigrok-cli byte for byte
#
# The programs are found next to this script first (<install_dir>/bin/ on a
# Pi, or a build folder the build copied it into), then, when it is run from
# node/capture/tools/, in ../build-pi-tests/ and then ../build/.
# Exit status: 0 when every check passes, 1 otherwise.
set -euo pipefail

usage() {
    cat <<EOF
usage: test_pi.sh [--keep]

  --keep    keep the work folder with every log and capture (it is always
            kept when a check fails)

Needs: pi5la, bitprobe, ratesweep and fdebugtest next to this script (or in
../build-pi-tests/ or ../build/), plus pinctrl, sigrok-cli and stty. The UART
check needs dtparam=uart0=on in /boot/firmware/config.txt and write access to
/dev/ttyAMA0 (the dialout group).
EOF
}

keep=0
while [[ $# -gt 0 ]]; do
    case "$1" in
    --keep) keep=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------- locations

here=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)
bin_dir=""
for dir in "$here" "$here/../build-pi-tests" "$here/../build"; do
    if [[ -x "$dir/pi5la" ]]; then
        bin_dir=$(cd "$dir" && pwd)
        break
    fi
done
if [[ -z "$bin_dir" ]]; then
    echo "test_pi: pi5la not found in $here, $here/../build-pi-tests or $here/../build" >&2
    echo "test_pi: build the Pi binaries first, or run the copy in <install_dir>/bin/" >&2
    exit 1
fi

missing=()
for tool in pi5la bitprobe ratesweep fdebugtest; do
    [[ -x "$bin_dir/$tool" ]] || missing+=("$bin_dir/$tool")
done
for tool in pinctrl sigrok-cli stty cmp; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if [[ ${#missing[@]} -gt 0 ]]; then
    echo "test_pi: missing: ${missing[*]}" >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/test_pi.XXXXXX")
uart_dev=/dev/ttyAMA0
gen_pid=""
cap_pid=""
nul_pid=""
gpio20_used=0

# Background jobs of a script start with SIGINT ignored, so they are stopped
# with SIGTERM; pi5la handles it like Ctrl-C and finishes cleanly.
stop_generator() {
    if [[ -n "$gen_pid" ]]; then
        kill -TERM "$gen_pid" 2>/dev/null || true
        wait "$gen_pid" 2>/dev/null || true
        gen_pid=""
    fi
}

# The generator, bitprobe and ratesweep leave GPIO 20 driving as an output.
# Make it an input again, so it cannot fight anything wired to it later.
release_gpio20() {
    pinctrl set 20 ip pd
}

# Called only by the EXIT trap, which shellcheck cannot see.
# shellcheck disable=SC2317
cleanup() {
    stop_generator
    local pid
    for pid in "$cap_pid" "$nul_pid"; do
        if [[ -n "$pid" ]]; then
            kill -TERM "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [[ $gpio20_used -eq 1 ]]; then
        release_gpio20 >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

# Show pinctrl output; a pinctrl failure is reported, never fatal.
show_pinctrl() {
    pinctrl "$@" || echo "(pinctrl $* failed)"
}

# ---------------------------------------------------------------- results

checks=(fdebugtest bitprobe-map bitprobe-verify ratesweep uart-loopback)
declare -A result=()
failed=0

record() {   # record CHECK PASS|FAIL DETAIL
    result[$1]=$2
    printf '%-16s %s  %s\n' "$1" "$2" "$3"
    if [[ "$2" != PASS ]]; then
        failed=1
    fi
}

summary() {
    local name
    echo
    echo "================ summary ================"
    for name in "${checks[@]}"; do
        printf '%-16s %s\n' "$name" "${result[$name]:-NOT RUN}"
    done
    echo "logs and captures: $work"
}

finish() {
    summary
    if [[ $failed -eq 0 && $keep -eq 0 ]]; then
        rm -rf "$work"
        echo "(work folder removed; pass --keep to keep it)"
    fi
    if [[ $failed -eq 0 ]]; then
        echo "ALL PASS"
        exit 0
    fi
    echo "FAILED"
    exit 1
}

uart_fail() {   # uart_fail DETAIL: the UART check cannot go on
    record uart-loopback FAIL "$1"
    finish
}

confirm() {   # confirm QUESTION: true only on "yes"
    local answer=""
    read -r -p "$1 Type yes to continue: " answer || true
    [[ "$answer" == yes ]]
}

abort_unconfirmed() {
    echo
    echo "Wiring not confirmed. Fix it, then run test_pi.sh again."
    failed=1
    finish
}

# ---------------------------------------------------------------- safety

cat <<'EOF'
================ HARDWARE SAFETY ================
Pi 5 GPIO is 3.3 V and NOT 5 V tolerant. Overvoltage on a GPIO pin damages
RP1, the Pi 5 southbridge, which cannot be replaced: it destroys the Pi.

These tests use loopback jumpers between Pi header pins ONLY.
  - Disconnect every external device, board, buffer and test lead.
  - Never connect pin 1 or 17 (3.3 V), pin 2 or 4 (5 V), or a ground pin
    to a GPIO.
  - Use one jumper at a time. Two outputs wired together fight each other.

Wiring used by this script (header pin -> header pin):
  step A  bitprobe, ratesweep  pin 38 (GPIO 20, generator) -> pin 3 (GPIO 2, channel 0)
  step B  UART loopback        pin 8  (GPIO 14, UART TXD0) -> pin 3 (GPIO 2, channel 0)
  Pin 3 is the 2nd hole of the odd row counted from the USB-C end. Pin 8 is
  the 4th hole of the even row; holes 1 and 2 of that row are 5 V. Pin 38 is
  the 19th hole of the even row.
=================================================
EOF
echo "programs: $bin_dir"
echo "work folder: $work"
echo
if ! confirm "Is nothing external connected to the Pi's GPIO header?"; then
    abort_unconfirmed
fi

# ---------------------------------------------------------------- fdebugtest

echo
echo "---- fdebugtest (no wiring needed)"
if "$bin_dir/fdebugtest" >"$work/fdebugtest.log" 2>&1 && grep -qx PASS "$work/fdebugtest.log"; then
    record fdebugtest PASS "RXSTALL detection works"
else
    record fdebugtest FAIL "see $work/fdebugtest.log"
fi

# ---------------------------------------------------------------- step A

echo
echo "---- step A: one jumper, pin 38 (GPIO 20) -> pin 3 (GPIO 2), nothing else"
read -r -p "Place the jumper, then press Enter: " _ || true

# After a reboot GPIO 2 has no function and `pinctrl lev 2` prints "-" until
# pi5la has captured once, so take a short capture first.
"$bin_dir/pi5la" --rate 1M --samples 1000 --out "$work/warmup.sr" >"$work/warmup.log" 2>&1 || true

echo "pinctrl lev 2-9,20 while a 1 Hz square wave runs on GPIO 20:"
gpio20_used=1
"$bin_dir/pi5la" --generator 1 --generator-only >"$work/generator.log" 2>&1 &
gen_pid=$!
sleep 1
for _ in 1 2 3 4 5 6; do
    show_pinctrl lev 2-9,20
    sleep 0.5
done
stop_generator
echo "GPIO 2 must change together with GPIO 20, and GPIO 3..9 must not follow it."
if ! confirm "Do GPIO 2 and GPIO 20 move together?"; then
    abort_unconfirmed
fi

echo
echo "---- bitprobe --probe: channel 0 in word bits 0 8 16 24, in that order"
if "$bin_dir/bitprobe" --probe >"$work/bitprobe-probe.log" 2>&1 &&
    grep -qx 'active bits (4, expected 4): 0 8 16 24' "$work/bitprobe-probe.log" &&
    grep -qx 'chronological order (slot 0 first): 0 8 16 24' "$work/bitprobe-probe.log"; then
    record bitprobe-map PASS "channel 0 = bits 0 8 16 24"
else
    record bitprobe-map FAIL "see $work/bitprobe-probe.log"
fi

echo
echo "---- bitprobe --verify 0: a clean 100 kHz wave on channel 0 only"
if "$bin_dir/bitprobe" --verify 0 --generator 100k --rate 10M --samples 16M \
        >"$work/bitprobe-verify.log" 2>&1 &&
    grep -qx PASS "$work/bitprobe-verify.log"; then
    record bitprobe-verify PASS "16M samples, every run 50 samples"
else
    record bitprobe-verify FAIL "see $work/bitprobe-verify.log"
fi

# 40M samples is 4 s and ten times the DMA ring, so a pass shows sustained
# throughput, not just a run that fit in the ring.
echo
echo "---- ratesweep: 8 channels at 10 MSa/s (divider 20), 0 stalls"
csv="$work/ratesweep.csv"
if "$bin_dir/ratesweep" --channels 8 --dividers 20 --samples 40M --out "$csv" \
        >"$work/ratesweep.log" 2>&1; then
    # channels,sample_rate,samples_captured,violations,violation_pct,stalls,status
    row=$(sed -n 2p "$csv")
    IFS=, read -r rs_ch rs_rate _ rs_viol _ rs_stalls rs_status <<<"$row" || true
    if [[ "${rs_ch:-}" == 8 && "${rs_rate:-}" == 10000000 && "${rs_stalls:-}" == 0 &&
          "${rs_viol:-}" == 0 && "${rs_status:-}" == ok ]]; then
        record ratesweep PASS "8 ch, 10 MSa/s, 0 violations, 0 stalls"
    else
        record ratesweep FAIL "CSV row: $row"
    fi
else
    record ratesweep FAIL "see $work/ratesweep.log"
fi

# ---------------------------------------------------------------- step B

# The last generator use is over: GPIO 20 must stop driving before TXD0 is
# wired to the same pin 3, in case the step-A jumper is still on.
if ! release_gpio20 >"$work/gpio20.log" 2>&1; then
    uart_fail "could not set GPIO 20 back to input (pinctrl set 20 ip pd), see $work/gpio20.log"
fi

echo
echo "---- step B: REMOVE the GPIO 20 jumper, then one jumper pin 8 (GPIO 14) -> pin 3 (GPIO 2)"
read -r -p "Move the jumper, then press Enter: " _ || true

if [[ ! -e "$uart_dev" ]]; then
    uart_fail "$uart_dev missing: add dtparam=uart0=on to /boot/firmware/config.txt and reboot"
fi
if [[ ! -w "$uart_dev" ]]; then
    uart_fail "no write access to $uart_dev: add this user to the dialout group, log in again"
fi

# GPIO 20 is now an input with a pull-down, and GPIO 2 has a pull-up to 3.3 V.
# If the step-A jumper is still on, GPIO 20 reads high through it.
gpio20_level=$(pinctrl lev 20 2>"$work/gpio20-lev.log" | tr -d '[:space:]') || gpio20_level=""
case "$gpio20_level" in
    0) echo "GPIO 20 reads low: the step-A jumper is off." ;;
    1) uart_fail "GPIO 20 reads high through pin 3: remove the pin 38 jumper and run again" ;;
    *) echo "pinctrl lev 20 printed '$gpio20_level'; check by eye that the pin 38 jumper is off." ;;
esac

echo "pinctrl get 14 (must be TXD0):"
show_pinctrl get 14
echo "pinctrl lev 2,14,20 with the line idle (GPIO 2 and 14 high, GPIO 20 low):"
show_pinctrl lev 2,14,20
# Confirm the wiring before TX drives anything.
if ! confirm "Is the only jumper pin 8 (GPIO 14) -> pin 3 (GPIO 2), with pin 38 free?"; then
    abort_unconfirmed
fi

# A pull-up makes GPIO 2 read high with or without the jumper, so check it
# against real traffic: NUL bytes at 300 baud hold the line low most of the time.
if ! stty -F "$uart_dev" 300 raw -echo 2>"$work/stty.log"; then
    uart_fail "stty -F $uart_dev 300 failed, see $work/stty.log"
fi
echo "pinctrl lev 2,14 while NUL bytes are sent at 300 baud:"
head -c 60 /dev/zero >"$uart_dev" &
nul_pid=$!
for _ in 1 2 3 4 5 6; do
    show_pinctrl lev 2,14
    sleep 0.25
done
wait "$nul_pid" || true
nul_pid=""
echo "GPIO 2 must read the same as GPIO 14 on every line, mostly low while sending."
if ! confirm "Do GPIO 2 and GPIO 14 move together?"; then
    abort_unconfirmed
fi

echo
echo "---- UART loopback: pi5la capture, sigrok-cli decode, byte-for-byte compare"
if ! stty -F "$uart_dev" 115200 raw -echo 2>"$work/stty.log"; then
    uart_fail "stty -F $uart_dev 115200 failed, see $work/stty.log"
fi
sent="$work/uart-sent.txt"
for i in $(seq -w 1 50); do
    printf 'test_pi UART loopback line %s ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n' "$i"
done >"$sent"

# One channel at 2 MSa/s (17 samples a bit) is plenty for 115200 baud and
# writes only 2 MB/s, so a slow SD card cannot cause a false FAIL. pi5la waits
# for the first change and stops once the line is quiet for 1 s.
"$bin_dir/pi5la" --channels 1 --rate 2M --duration 10s --silence-timeout 1s \
    --out "$work/uart.sr" >"$work/uart-capture.log" 2>&1 &
cap_pid=$!
sleep 1
send_ok=1
cat "$sent" >"$uart_dev" 2>"$work/uart-send.log" || send_ok=0
cap_rc=0
if [[ $send_ok -eq 0 ]]; then
    kill -TERM "$cap_pid" 2>/dev/null || true
fi
wait "$cap_pid" || cap_rc=$?
cap_pid=""

if [[ $send_ok -eq 0 ]]; then
    record uart-loopback FAIL "writing to $uart_dev failed, see $work/uart-send.log"
elif [[ $cap_rc -ne 0 ]]; then
    record uart-loopback FAIL "pi5la exit $cap_rc, see $work/uart-capture.log"
elif ! sigrok-cli -i "$work/uart.sr" -P uart:rx=GPIO2:baudrate=115200 -B uart=rx \
        >"$work/uart-decoded.txt" 2>"$work/uart-decode.log"; then
    record uart-loopback FAIL "sigrok-cli failed, see $work/uart-decode.log"
elif cmp "$sent" "$work/uart-decoded.txt" >"$work/uart-cmp.log" 2>&1; then
    record uart-loopback PASS "$(wc -c <"$sent") bytes decoded byte for byte"
else
    record uart-loopback FAIL "decode differs: cmp $sent $work/uart-decoded.txt"
fi

finish
