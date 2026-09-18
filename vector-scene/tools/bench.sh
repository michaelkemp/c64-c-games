#!/usr/bin/env bash
# Runs a benchmark .prg directly in x64sc (autostart injects it straight
# into memory -- no slow emulated-1541 disk load) under VICE's remote
# monitor, and reads the result back out of emulated memory -- no manual
# screen-reading, no OCR. See README.md's "Automated benchmarking" section
# for why this exists and how the pause/resume protocol, and the need for
# -warp/-sounddev dummy, were verified before trusting any number out of
# it: without them x64sc paces itself to real 1x (or slower) wall-clock
# speed via its audio sync, so a benchmark that's genuinely ~0.5 real
# seconds of C64 time can take 20+ real seconds to actually finish --
# easy to mistake for a hang if you only sample once.
#
# Usage: tools/bench.sh build/bench-pair-baseline.prg [max_wait_seconds]

set -euo pipefail

PRG="${1:?usage: bench.sh <prg-path> [max_wait_seconds]}"
MAX_WAIT="${2:-20}"
MONITOR_PORT=6510
RESULT_ADDR=3F00 # hex, matches tests/bench_report.h's BENCH_RESULT_ADDR (4 cycle bytes) -- cfg/lowmem.cfg's linker-reserved SCRATCH region, not a guessed address (see that file's comment for why)
FLAG_ADDR=3F04   # hex, RESULT_ADDR+4: the ready flag byte
READY_BYTE=a5    # hex, matches tests/bench_report.h's BENCH_READY_BYTE

if [[ ! -f "$PRG" ]]; then
    echo "error: $PRG not found (build it first)" >&2
    exit 1
fi

x64sc -warp -sounddev dummy -remotemonitor -autostart "$PRG" >/tmp/vice-bench.log 2>&1 &
VICE_PID=$!
trap 'kill "$VICE_PID" 2>/dev/null || true; wait "$VICE_PID" 2>/dev/null || true' EXIT

# Wait for the remote monitor socket to accept connections.
for _ in $(seq 1 40); do
    nc -z localhost "$MONITOR_PORT" 2>/dev/null && break
    sleep 0.25
done

# Let autostart's injected RUN actually begin before the first connect --
# connecting pauses the CPU, and connecting too early can land inside the
# autostart machinery itself rather than our program.
sleep 2

# Connecting pauses the CPU; resume immediately so the benchmark can run.
printf 'x\n' | nc -q1 localhost "$MONITOR_PORT" >/dev/null || true

elapsed=0
step=1
while (( elapsed < MAX_WAIT )); do
    sleep "$step"
    elapsed=$((elapsed + step))

    # Reconnecting pauses the CPU again; check the ready flag, then resume.
    # VICE's "m" reply looks like: >C:2004  ff                    .
    out=$(printf 'm %s %s\nx\n' "$FLAG_ADDR" "$FLAG_ADDR" | nc -q1 localhost "$MONITOR_PORT" || true)
    flag=$(echo "$out" | grep -oE '>C:[0-9a-f]+ +[0-9a-f]{2}' | head -1 | awk '{print $2}')

    if [[ "${flag,,}" == "$READY_BYTE" ]]; then
        result=$(printf 'm %04x %04x\nquit\n' "$((0x$RESULT_ADDR))" "$((0x$RESULT_ADDR + 3))" | nc -q1 localhost "$MONITOR_PORT" || true)
        bytes_line=$(echo "$result" | grep -oE '>C:[0-9a-f]+ +[0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2} +[0-9a-f]{2}' | head -1)
        b0=$((16#$(echo "$bytes_line" | awk '{print $2}')))
        b1=$((16#$(echo "$bytes_line" | awk '{print $3}')))
        b2=$((16#$(echo "$bytes_line" | awk '{print $4}')))
        b3=$((16#$(echo "$bytes_line" | awk '{print $5}')))
        cycles=$(( (b0 << 24) | (b1 << 16) | (b2 << 8) | b3 ))
        echo "$cycles"
        exit 0
    fi
done

echo "error: benchmark did not signal ready within ${MAX_WAIT}s" >&2
echo "--- vice log ---" >&2
cat /tmp/vice-bench.log >&2
exit 1
