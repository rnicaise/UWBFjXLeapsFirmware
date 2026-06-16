#!/usr/bin/env python3
"""Sweep relative DW3000 TX power levels on the current CH5/6M8/PLEN128 profile."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import time
from pathlib import Path

import serial

from uwb_filter_experiment import Sample, parse_sample, write_capture


LEVELS = {
    0: "0x7f7f7f7f",
    1: "0x9f9f9f9f",
    2: "0xbfbfbfbf",
    3: "0xdfdfdfdf",
    4: "0xfdfdfdfd",
}

SUMMARY_HEADER = [
    "level",
    "tx_power",
    "state",
    "signal",
    "samples",
    "valid",
    "valid_pct",
    "hz",
    "mean_m",
    "std_cm",
    "span95_cm",
    "max_jump_cm",
    "jumps_gt_10cm",
    "jumps_gt_20cm",
    "nlos_mean",
    "peak_fp_mean",
    "rx_fp_gap_mean",
    "rx_power_mean",
    "fp_power_mean",
    "path",
]


def serial_write_command(ser: serial.Serial, command: str, wait_s: float = 1.5) -> list[str]:
    ser.write((command + "\r\n").encode("ascii"))
    ser.flush()
    deadline = time.time() + wait_s
    replies: list[str] = []
    while time.time() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        if line.startswith(("ACK,", "ERR,", "PROFILE,", "TXPWR,", "ROLE,")):
            replies.append(line)
            if line.startswith(("ACK,", "ERR,")) or line.startswith(command.replace("CFG,GET_", "")):
                break
    return replies


def wait_for_reply(ser: serial.Serial, prefixes: tuple[str, ...], wait_s: float) -> list[str]:
    deadline = time.time() + wait_s
    replies: list[str] = []
    while time.time() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if line.startswith(prefixes):
            replies.append(line)
            if line.startswith(prefixes):
                return replies
    return replies


def set_tx_power_level(ser: serial.Serial, level: int, attempts: int = 4) -> list[str]:
    expected_ack = f"ACK,TXPWR,{level},"
    expected_get = f"TXPWR,{level},"
    replies: list[str] = []
    for _ in range(attempts):
        ser.reset_input_buffer()
        ser.write(f"CFG,TXPWR,{level}\r\n".encode("ascii"))
        ser.flush()
        deadline = time.time() + 2.0
        while time.time() < deadline:
            line = ser.readline().decode("ascii", errors="replace").strip()
            if not line.startswith(("ACK,", "ERR,", "TXPWR,")):
                continue
            replies.append(line)
            if line.startswith(expected_ack):
                break
        ser.reset_input_buffer()
        ser.write(b"CFG,GET_TXPWR\r\n")
        ser.flush()
        deadline = time.time() + 2.0
        while time.time() < deadline:
            line = ser.readline().decode("ascii", errors="replace").strip()
            if not line.startswith(("ACK,", "ERR,", "TXPWR,")):
                continue
            replies.append(line)
            if line.startswith(expected_get):
                return replies
    raise RuntimeError(f"could not confirm TX power level {level}; replies={replies}")


def capture_from_open_serial(ser: serial.Serial, seconds: float) -> list[Sample]:
    samples: list[Sample] = []
    bad = 0
    start = time.time()
    while time.time() - start < seconds:
        line = ser.readline().decode("ascii", errors="replace").strip()
        parsed = parse_sample(line)
        if parsed is None:
            if line and not line.startswith(("#", "ACK,", "ERR,", "PROFILE,", "TXPWR,", "ROLE,")):
                bad += 1
            continue
        samples.append(parsed)
    if bad:
        print(f"ignored_malformed_lines={bad}")
    return samples


def finite_mean(values: list[float | None]) -> float:
    finite = [value for value in values if value is not None and math.isfinite(value)]
    return statistics.fmean(finite) if finite else math.nan


def summarize_values(level: int, tx_power: str, state: str, signal: str, values: list[float], samples: list[Sample], hz: float, path: Path) -> list[str]:
    valid_count = len(values)
    total_count = len(samples)
    if values:
        sorted_values = sorted(values)
        diffs = [abs(b - a) for a, b in zip(values, values[1:])]
        mean_m = statistics.fmean(values)
        std_cm = statistics.pstdev(values) * 100.0
        span95_cm = (sorted_values[int(0.95 * len(sorted_values))] - sorted_values[int(0.05 * len(sorted_values))]) * 100.0
        max_jump_cm = max(diffs) * 100.0 if diffs else 0.0
        jumps_gt_10cm = sum(1 for diff in diffs if diff > 0.10)
        jumps_gt_20cm = sum(1 for diff in diffs if diff > 0.20)
    else:
        mean_m = std_cm = span95_cm = max_jump_cm = math.nan
        jumps_gt_10cm = jumps_gt_20cm = 0

    return [
        str(level),
        tx_power,
        state,
        signal,
        str(total_count),
        str(valid_count),
        f"{(100.0 * valid_count / total_count) if total_count else 0.0:.2f}",
        f"{hz:.2f}",
        f"{mean_m:.4f}" if math.isfinite(mean_m) else "nan",
        f"{std_cm:.2f}" if math.isfinite(std_cm) else "nan",
        f"{span95_cm:.2f}" if math.isfinite(span95_cm) else "nan",
        f"{max_jump_cm:.2f}" if math.isfinite(max_jump_cm) else "nan",
        str(jumps_gt_10cm),
        str(jumps_gt_20cm),
        f"{finite_mean([sample.nlos for sample in samples]):.2f}",
        f"{finite_mean([sample.peak_fp for sample in samples]):.2f}",
        f"{finite_mean([(sample.rx_power - sample.fp_power) if sample.rx_power is not None and sample.fp_power is not None else None for sample in samples]):.2f}",
        f"{finite_mean([sample.rx_power for sample in samples]):.1f}",
        f"{finite_mean([sample.fp_power for sample in samples]):.1f}",
        str(path),
    ]


def summarize_capture(level: int, path: Path, samples: list[Sample]) -> list[list[str]]:
    if len(samples) >= 2:
        duration_ms = samples[-1].ms - samples[0].ms
        hz = (len(samples) - 1) * 1000.0 / duration_ms if duration_ms > 0 else 0.0
    else:
        hz = 0.0

    state = "ok" if samples else "no_samples"
    valid_samples = [sample for sample in samples if sample.valid]
    raw = [sample.dist for sample in valid_samples]
    firmware_median = [sample.dist_filt for sample in valid_samples if sample.dist_filt is not None]
    firmware_smooth = [sample.dist_smooth for sample in valid_samples if sample.dist_smooth is not None]
    rows = [summarize_values(level, LEVELS[level], state, "raw", raw, samples, hz, path)]
    rows.append(summarize_values(level, LEVELS[level], state, "firmware_median5", firmware_median, samples, hz, path))
    rows.append(summarize_values(level, LEVELS[level], state, "firmware_smooth", firmware_smooth, samples, hz, path))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbserial-0001")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--seconds", type=float, default=45.0)
    parser.add_argument("--settle-seconds", type=float, default=2.0)
    parser.add_argument("--levels", default="0,1,2,3,4")
    parser.add_argument("--out-dir", type=Path, default=Path("experiments"))
    parser.add_argument("--date", default=time.strftime("%Y%m%d"))
    args = parser.parse_args()

    levels = [int(part) for part in args.levels.split(",") if part]
    unsupported = [level for level in levels if level not in LEVELS]
    if unsupported:
        raise SystemExit(f"unsupported TX power levels: {unsupported}")

    summary_path = args.out_dir / f"uwb-txpower-sweep-{args.date}-summary.csv"
    all_rows: list[list[str]] = []

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        ser.reset_input_buffer()
        print("command CFG,PROFILE,35", serial_write_command(ser, "CFG,PROFILE,35"))
        time.sleep(args.settle_seconds)
        for level in levels:
            path = args.out_dir / f"uwb-txpower-l{level}-ch5_6m8_plen128_pac8-{args.date}.csv"
            replies = set_tx_power_level(ser, level)
            print(f"command CFG,TXPWR,{level}", replies)
            time.sleep(args.settle_seconds)
            samples = capture_from_open_serial(ser, args.seconds)
            write_capture(path, samples)
            all_rows.extend(summarize_capture(level, path, samples))
            print(f"level={level} samples={len(samples)} path={path}")

        print("command CFG,TXPWR,4", set_tx_power_level(ser, 4))

    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(SUMMARY_HEADER)
        writer.writerows(all_rows)
    print(f"summary={summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())