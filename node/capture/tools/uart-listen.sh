#!/bin/sh
# Live UART sniffer: pi5la --stream piped into sigrok-cli's UART decoder.
# Listens until Ctrl-C.
#
#   uart-listen                     text, 115200 baud, channel 0 (GPIO 2)
#   uart-listen --hex               one line per byte: sample number and hex
#   uart-listen --baud 9600 --channel 3 --rate 1M
#
# sigrok's decoders run in Python and may not keep up with high sample rates.
# If pi5la reports a DMA ring overflow, lower --rate: about 16x-20x the baud
# rate is plenty for UART.
set -eu

baud=115200
channel=0
rate=2M
base=2
hex=0

usage() {
    cat <<EOF
usage: uart-listen [--hex] [--baud BAUD] [--channel N] [--rate RATE] [--base-gpio G]

  --baud BAUD     UART baud rate, 8N1 (default 115200)
  --channel N     capture channel carrying the UART line, 0-7 (default 0 = GPIO 2)
  --rate RATE     sample rate, suffix k/M (default 2M)
  --base-gpio G   GPIO of channel 0 (default 2)
  --hex           show each byte as "sample-number: hex" instead of text
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
    --baud) baud=$2; shift 2 ;;
    --channel) channel=$2; shift 2 ;;
    --rate) rate=$2; shift 2 ;;
    --base-gpio) base=$2; shift 2 ;;
    --hex) hex=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 1 ;;
    esac
done

case "$channel" in
    [0-7]) ;;
    *) echo "uart-listen: --channel must be 0-7" >&2; exit 1 ;;
esac

# pi5la next to this script (build directory), else from PATH, unless $PI5LA is set.
here=$(dirname "$0")
if [ -z "${PI5LA:-}" ]; then
    if [ -x "$here/pi5la" ]; then PI5LA=$here/pi5la; else PI5LA=pi5la; fi
fi

command -v sigrok-cli >/dev/null 2>&1 || {
    echo "uart-listen: sigrok-cli not found (sudo apt install sigrok-cli)" >&2
    exit 1
}

# sigrok's binary input wants the sample rate in plain Hz.
rate_hz=$(printf '%s\n' "$rate" | awk '{
    v = $0; m = 1
    if (v ~ /M$/) { m = 1000000; sub(/M$/, "", v) }
    else if (v ~ /k$/) { m = 1000; sub(/k$/, "", v) }
    if (v !~ /^[0-9]+(\.[0-9]+)?$/) exit 1
    printf "%.0f", v * m
}') || { echo "uart-listen: invalid --rate '$rate'" >&2; exit 1; }

echo "uart-listen: GPIO $((base + channel)) (channel $channel), $baud baud 8N1, sampling at $rate_hz Hz. Ctrl-C to stop." >&2

input="binary:numchannels=8:samplerate=$rate_hz"
decoder="uart:rx=$channel:baudrate=$baud"

if [ "$hex" -eq 1 ]; then
    "$PI5LA" --rate "$rate" --base-gpio "$base" --stream |
        sigrok-cli -I "$input" -i - -P "$decoder:format=hex" -A uart=rx-data \
            --protocol-decoder-samplenum
else
    "$PI5LA" --rate "$rate" --base-gpio "$base" --stream |
        sigrok-cli -I "$input" -i - -P "$decoder" -B uart=rx
fi
