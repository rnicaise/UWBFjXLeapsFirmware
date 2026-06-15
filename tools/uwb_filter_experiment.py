#!/usr/bin/env python3
"""Capture UWB serial CSV and compare distance filters offline."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path

import serial


CSV_HEADER = [
    "ms",
    "sample",
    "dist",
    "rx_power",
    "fp_power",
    "clock_ppm",
    "score",
    "nlos",
    "peak_fp",
    "fp_conf",
    "sts",
    "iax",
    "iay",
    "iaz",
    "rax",
    "ray",
    "raz",
    "resp_acq_ms",
    "init_acq_ms",
    "resp_profile_opt",
    "init_profile_opt",
    "igx",
    "igy",
    "igz",
    "rgx",
    "rgy",
    "rgz",
    "valid",
    "dist_filt",
    "dist_smooth",
]


@dataclass
class Sample:
    ms: int
    sample: int
    dist: float
    rx_power: float | None
    fp_power: float | None
    clock_ppm: float | None
    nlos: float | None
    peak_fp: float | None
    fp_conf: int | None
    sts: int | None
    valid: bool
    dist_filt: float | None
    dist_smooth: float | None
    row: list[str]


def finite_float(value: str) -> float | None:
    try:
        parsed = float(value)
    except ValueError:
        return None
    return parsed if math.isfinite(parsed) else None


def parse_sample(line: str) -> Sample | None:
    if not line or line.startswith("#") or line.startswith(("ROLE,", "ACK,", "ERR,")):
        return None

    parts = line.split(",")
    if len(parts) < 29:
        return None

    try:
        return Sample(
            ms=int(parts[0]),
            sample=int(parts[1]),
            dist=float(parts[2]),
            rx_power=finite_float(parts[3]),
            fp_power=finite_float(parts[4]),
            clock_ppm=finite_float(parts[5]),
            nlos=finite_float(parts[7]),
            peak_fp=finite_float(parts[8]),
            fp_conf=int(parts[9]),
            sts=int(parts[10]),
            valid=parts[27] != "0",
            dist_filt=finite_float(parts[28]),
            dist_smooth=finite_float(parts[29]) if len(parts) > 29 else None,
            row=(parts[:30] if len(parts) > 29 else parts[:29] + [""]),
        )
    except (ValueError, IndexError):
        return None


def capture(port: str, baud: int, seconds: float) -> list[Sample]:
    samples: list[Sample] = []
    bad = 0
    with serial.Serial(port, baud, timeout=0.2) as ser:
        ser.reset_input_buffer()
        start = time.time()
        while time.time() - start < seconds:
            line = ser.readline().decode("ascii", errors="replace").strip()
            parsed = parse_sample(line)
            if parsed is None:
                if line and not line.startswith("#"):
                    bad += 1
                continue
            samples.append(parsed)
    if bad:
        print(f"ignored_malformed_lines={bad}")
    return samples


def write_capture(path: Path, samples: list[Sample]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(CSV_HEADER)
        for sample in samples:
            writer.writerow(sample.row)


def median(values: list[float]) -> float:
    sorted_values = sorted(values)
    return sorted_values[len(sorted_values) // 2]


def rolling_median(values: list[float], window: int) -> list[float]:
    out: list[float] = []
    buf: deque[float] = deque(maxlen=window)
    for value in values:
        buf.append(value)
        out.append(median(list(buf)))
    return out


def rolling_mean(values: list[float], window: int) -> list[float]:
    out: list[float] = []
    buf: deque[float] = deque(maxlen=window)
    total = 0.0
    for value in values:
        if len(buf) == window:
            total -= buf[0]
        buf.append(value)
        total += value
        out.append(total / len(buf))
    return out


def rolling_trimmed(values: list[float], window: int, trim: int) -> list[float]:
    out: list[float] = []
    buf: deque[float] = deque(maxlen=window)
    for value in values:
        buf.append(value)
        sorted_values = sorted(buf)
        if len(sorted_values) > 2 * trim:
            sorted_values = sorted_values[trim:-trim]
        out.append(statistics.fmean(sorted_values))
    return out


def ewma(values: list[float], alpha: float) -> list[float]:
    out: list[float] = []
    estimate: float | None = None
    for value in values:
        estimate = value if estimate is None else (alpha * value + (1.0 - alpha) * estimate)
        out.append(estimate)
    return out


def alpha_beta(values: list[float], hz: float, alpha: float, beta: float) -> list[float]:
    dt = 1.0 / hz if hz > 0 else 0.0025
    pos: float | None = None
    vel = 0.0
    out: list[float] = []
    for measurement in values:
        if pos is None:
            pos = measurement
        else:
            pred = pos + vel * dt
            residual = measurement - pred
            pos = pred + alpha * residual
            vel = vel + (beta * residual / dt)
        out.append(pos)
    return out


def quality_weighted(values: list[float], samples: list[Sample], window: int) -> list[float]:
    out: list[float] = []
    value_buf: deque[float] = deque(maxlen=window)
    weight_buf: deque[float] = deque(maxlen=window)
    for value, sample in zip(values, samples):
        weight = 1.0
        if sample.nlos is not None:
            weight *= max(0.1, min(1.0, (10.0 - sample.nlos) / 5.0))
        if sample.rx_power is not None and sample.fp_power is not None:
            gap = abs(sample.rx_power - sample.fp_power)
            weight *= max(0.2, min(1.0, 1.0 - max(0.0, gap - 3.0) / 12.0))
        if sample.clock_ppm is not None:
            weight *= max(0.2, min(1.0, 1.0 - abs(sample.clock_ppm) / 25.0))
        if sample.peak_fp is not None and sample.peak_fp > 3.0:
            weight *= 0.5
        if sample.fp_conf is not None and sample.fp_conf <= 0:
            weight *= 0.8
        value_buf.append(value)
        weight_buf.append(weight)
        denom = sum(weight_buf)
        out.append(sum(v * w for v, w in zip(value_buf, weight_buf)) / denom if denom > 0 else value)
    return out


def metrics(name: str, values: list[float], hz: float, latency_samples: float) -> dict[str, float | str]:
    if not values:
        return {"name": name}
    sorted_values = sorted(values)
    count = len(values)
    diffs = [abs(b - a) for a, b in zip(values, values[1:])]
    return {
        "name": name,
        "std_cm": statistics.pstdev(values) * 100.0,
        "span95_cm": (sorted_values[int(0.95 * count)] - sorted_values[int(0.05 * count)]) * 100.0,
        "max_jump_cm": (max(diffs) * 100.0) if diffs else 0.0,
        "jumps_gt_10cm": float(sum(1 for diff in diffs if diff > 0.10)),
        "jumps_gt_20cm": float(sum(1 for diff in diffs if diff > 0.20)),
        "latency_ms_est": (latency_samples / hz * 1000.0) if hz > 0 else 0.0,
        "mean_m": statistics.fmean(values),
    }


def candidate_filters(samples: list[Sample]) -> list[tuple[str, list[float], float]]:
    raw = [sample.dist for sample in samples if sample.valid]
    valid_samples = [sample for sample in samples if sample.valid]
    candidates: list[tuple[str, list[float], float]] = [("raw", raw, 0.0)]

    firmware = [sample.dist_filt for sample in valid_samples if sample.dist_filt is not None]
    if len(firmware) == len(raw):
        candidates.append(("firmware_median5", firmware, 2.0))
    firmware_smooth = [sample.dist_smooth for sample in valid_samples if sample.dist_smooth is not None]
    if len(firmware_smooth) == len(raw):
        candidates.append(("firmware_smooth", firmware_smooth, 20.0))

    for window in (3, 5, 7, 9, 11, 15):
        candidates.append((f"median{window}", rolling_median(raw, window), window / 2.0))
    for window in (5, 10, 15, 21, 31, 41):
        candidates.append((f"mean{window}", rolling_mean(raw, window), window / 2.0))
    for window, trim in ((9, 1), (15, 2), (21, 3), (31, 5)):
        candidates.append((f"trimmed{window}_drop{trim}", rolling_trimmed(raw, window, trim), window / 2.0))
    for alpha in (0.08, 0.12, 0.18, 0.25, 0.35):
        candidates.append((f"ewma{alpha:.2f}", ewma(raw, alpha), 1.0 / alpha))

    median5 = rolling_median(raw, 5)
    candidates.append(("median5_trimmed21_drop3", rolling_trimmed(median5, 21, 3), 12.5))
    candidates.append(("median5_ewma0.18", ewma(median5, 0.18), 7.5))
    candidates.append(("quality_weighted15", quality_weighted(raw, valid_samples, 15), 7.5))
    candidates.append(("median5_quality_weighted15", quality_weighted(median5, valid_samples, 15), 9.5))
    return candidates


def print_report(samples: list[Sample]) -> None:
    if len(samples) < 2:
        print("not enough samples")
        return
    duration_ms = samples[-1].ms - samples[0].ms
    hz = (len(samples) - 1) * 1000.0 / duration_ms if duration_ms > 0 else 0.0
    valid_count = sum(1 for sample in samples if sample.valid)
    print(f"samples={len(samples)} valid={valid_count} hz={hz:.1f} duration_s={duration_ms / 1000.0:.1f}")

    rows = [metrics(name, values, hz, latency) for name, values, latency in candidate_filters(samples)]
    rows = sorted(rows, key=lambda row: (float(row.get("jumps_gt_20cm", 0.0)), float(row.get("span95_cm", 999.0)), float(row.get("latency_ms_est", 999.0))))
    print("\nrank,name,std_cm,span95_cm,max_jump_cm,jumps_gt_10cm,jumps_gt_20cm,latency_ms_est,mean_m")
    for rank, row in enumerate(rows, 1):
        print(
            f"{rank},{row['name']},{row['std_cm']:.2f},{row['span95_cm']:.2f},"
            f"{row['max_jump_cm']:.2f},{row['jumps_gt_10cm']:.0f},{row['jumps_gt_20cm']:.0f},"
            f"{row['latency_ms_est']:.1f},{row['mean_m']:.3f}"
        )


def load_capture(path: Path) -> list[Sample]:
    samples: list[Sample] = []
    with path.open(newline="") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        for row in reader:
            parsed = parse_sample(",".join(row))
            if parsed is not None:
                samples.append(parsed)
    return samples


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbserial-0001")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--input", type=Path, default=None, help="Analyze an existing capture CSV instead of capturing serial")
    args = parser.parse_args()

    if args.input is not None:
        samples = load_capture(args.input)
        print(f"loaded={args.input}")
    else:
        samples = capture(args.port, args.baud, args.seconds)
        out = args.out or Path("experiments") / f"uwb-static-{time.strftime('%Y%m%d-%H%M%S')}.csv"
        write_capture(out, samples)
        print(f"capture={out}")

    print_report(samples)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())