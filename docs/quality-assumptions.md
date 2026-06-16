# UWB Quality Assumptions

This document captures the working assumptions used to improve UWB ranging precision in this project. It focuses on variation/noise reduction, not absolute calibration.

## Current Baseline

The current fast SS-TWR profile can produce roughly 390-430 Hz on the bench after UART pipelining. This gives enough temporal density to publish multiple distance views:

- `dist`: raw measured distance.
- `dist_filt`: fast firmware median-of-5, intended to kill isolated 1-2 sample spikes with low latency.
- `dist_smooth`: slower/sticky precision output, intended for display/static precision and plateau rejection. It must not be treated as the final airbag trigger signal without dynamic validation.

## Static Plateau Observation

Capture: `experiments/uwb-static-1m-20260615-101332.csv`.

Setup: two boxes nominally static around 1 m. Absolute distance was not the goal; variation and false jumps were.

Summary:

- 25,414 rows over 59.9 s.
- Raw stream around 424 Hz.
- 18,610 firmware-valid rows.
- The signal did not behave like simple Gaussian noise.
- It stayed around a stable low plateau near 1.74 m for about 15 s, then moved into a stable false high plateau near 2.8 m.

5 s valid-bin summary from that capture:

| Time window | Valid % | Mean m | P05 m | Median m | P95 m | Std cm |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0-5 s | 100.0 | 1.737 | 1.68 | 1.74 | 1.79 | 3.4 |
| 5-10 s | 100.0 | 1.735 | 1.68 | 1.74 | 1.79 | 3.4 |
| 10-15 s | 100.0 | 1.736 | 1.70 | 1.74 | 1.79 | 2.8 |
| 15-20 s | 76.0 | 2.109 | 1.62 | 1.75 | 2.85 | 51.6 |
| 20-25 s | 56.1 | 2.779 | 2.69 | 2.80 | 2.85 | 11.0 |
| 25-30 s | 62.9 | 2.785 | 2.69 | 2.80 | 2.86 | 11.5 |
| 30-35 s | 50.9 | 2.532 | 1.95 | 2.79 | 2.89 | 37.8 |
| 35-40 s | 55.4 | 2.680 | 2.03 | 2.79 | 2.85 | 26.5 |
| 40-45 s | 67.2 | 2.784 | 2.71 | 2.80 | 2.84 | 7.4 |
| 45-50 s | 68.7 | 2.781 | 2.70 | 2.80 | 2.84 | 8.8 |
| 50-55 s | 72.6 | 2.793 | 2.73 | 2.80 | 2.84 | 3.3 |
| 55-60 s | 68.5 | 2.780 | 2.72 | 2.80 | 2.84 | 11.1 |

Accepted valid jumps >20 cm: 310.

## What NLOS Showed

NLOS was informative, but not by itself sufficient as a clean binary rejection rule.

Average radio metrics by valid distance band:

| Distance band m | Count | RX dBm | FP dBm | RX-FP gap dB | NLOS | Peak-to-FP | Clock ppm |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1.0-1.5 | 44 | -79.5 | -100.8 | 21.3 | 2.6 | 6.36 | -1.15 |
| 1.5-2.0 | 7,450 | -79.4 | -96.3 | 16.9 | 3.7 | 5.22 | -1.16 |
| 2.0-2.5 | 705 | -79.6 | -92.2 | 12.7 | 5.0 | 3.78 | -1.15 |
| 2.5-3.0 | 10,411 | -79.5 | -82.2 | 2.7 | 6.9 | 1.72 | -1.15 |

Interpretation:

- Low plateau / shorter-distance region had lower NLOS but a very large `rx_power - fp_power` gap and high `peak_to_fp`.
- High plateau / longer-distance region had higher NLOS, much lower `peak_to_fp`, and a small RX-FP gap.
- This looks like a path-regime change, not random measurement noise.
- The high plateau may be a strong late/reflected path becoming dominant, or the receiver tracking a different path cluster.
- `nlos >= 6` correlates with the false high plateau, but rejecting solely on NLOS would also be brittle. It should be combined with innovation gating, `peak_to_fp`, RX-FP gap, and temporal consistency.

Working heuristic from this capture:

- Suspicious high plateau signature: `nlos >= 6`, `peak_to_fp <= 2.5`, and `abs(rx_power - fp_power) <= 5 dB`.
- Suspicious transition signature: repeated valid jumps >20 cm while radio metrics change regime.

These thresholds are hypotheses from one static capture, not final production constants.

## Lexicon

### Gate

A gate is a plausibility barrier. It decides whether a measurement should be accepted, flagged, or ignored.

Examples:

- Reject distances outside a physical range.
- Reject jumps that imply impossible relative speed.
- Reject a new value if it is too far from the current tracker estimate.

Why it helps: it blocks physically implausible values and isolated radio glitches.

Limit: a persistent false plateau can eventually look plausible unless the gate is tied to a tracker or radio-quality evidence.

### Diagnostics

Diagnostics are DW3000 radio-quality metrics read after each exchange.

Useful fields in this project:

- `rx_power`: total received power.
- `fp_power`: first-path power.
- `rx_power - fp_power`: rough indication of how dominant non-first-path energy may be.
- `peak_to_fp`: distance between the main peak and first path in accumulator samples.
- `fp_conf`: first-path confidence level.
- `nlos`: non-line-of-sight / multipath score.
- `clock_ppm`: initiator/responder clock offset estimate.

Why it helps: two distances can have similar numeric values but very different radio credibility.

### Median

A median sorts a small window and keeps the middle value.

Why it helps: it removes isolated spikes cheaply.

Limit: if a false value persists for most of the window, the median follows it.

### STS

STS means Scrambled Timestamp Sequence.

It adds a known scrambled sequence that helps the DW3000 timestamp and validate receptions more robustly.

Why it may help:

- Better timestamp robustness.
- Better behavior in multipath.
- More confidence in the received packet/timing.

Cost:

- More airtime.
- Symmetric configuration required on both boards.
- Potentially lower ranging rate.

### Preamble

The preamble is the known signal sent before the packet payload. The receiver uses it to detect the packet, synchronize, and estimate arrival time.

Examples:

- `PLEN_128`: short and fast.
- `PLEN_256` / `PLEN_512`: more robust.
- `PLEN_1024`: robust but slower.

Why it may help: a longer preamble gives the receiver more signal structure to identify the first path and synchronize cleanly.

Cost: longer airtime and lower maximum update rate.

### PAC

PAC means Preamble Acquisition Chunk. It controls how the receiver processes chunks of the preamble.

Common pairing intuition:

- `PLEN_128 + PAC8`: fast profile.
- `PLEN_256 + PAC16`: more robust profile.
- `PLEN_1024 + PAC32`: robust/slow profile.

PAC should be adjusted with preamble length rather than changed in isolation.

### Channel

The channel is the UWB RF band used by the DW3000.

In this project the important candidates are:

- `CH5`: current common baseline.
- `CH9`: alternative channel already represented by a runtime profile.

Why it may help: multipath and antenna/environment behavior can change strongly with RF channel. A false plateau seen on one channel may be weaker or absent on another.

### Data Rate

Data rate is the packet data speed.

In this project:

- `6M8`: 6.8 Mbps, fast, high-rate.
- `850K`: 850 kbps, slower, potentially more robust.

Why it may help: a slower robust profile can improve reception margin and first-path behavior, but it reduces Hz.

### TX Power

TX power is radio transmit power.

Too low:

- Direct path may become weak.
- Packet loss can increase.

Too high:

- Reflections can become stronger too.
- Short-range measurements can become less clean.
- A reflected path may dominate.

TX power must stay within regulatory and Qorvo configuration constraints.

The 2026-06-15 firmware sweep uses relative TX power register levels, not calibrated dBm values:

| Level | TX_POWER register |
| ---: | --- |
| 0 | `0x7f7f7f7f` |
| 1 | `0x9f9f9f9f` |
| 2 | `0xbfbfbfbf` |
| 3 | `0xdfdfdfdf` |
| 4 | `0xfdfdfdfd` |

Level 4 is the previous/current Qorvo CH5 default used by this project. Treat lower levels as experimental bench settings until regulatory and product constraints are reviewed.

### First Path

The first path is the earliest arriving radio path. Ideally it is the direct antenna-to-antenna path.

Correct ranging depends on timestamping the first path, not necessarily the strongest path.

Problem: in multipath, a later reflected path can be stronger than the direct path. The measured distance can then jump too long.

### Multipath

Multipath means the same packet arrives through several paths: direct, table reflection, wall reflection, body reflection, cable/battery reflection, etc.

Why it matters: the receiver must identify the first path inside a mixture of arrivals.

### NLOS

NLOS means Non-Line-Of-Sight. The direct path is blocked, weak, or not dominant.

NLOS does not always mean no measurement. It means lower trust in the timestamp/distance.

### Multi-Exchange Filtering

Multi-exchange filtering combines several consecutive SS-TWR measurements.

Examples:

- Median of 3 or 5 exchanges.
- Trimmed mean of a short window.
- N-of-M confirmation for safety logic.

At 400 Hz, 5 exchanges are roughly 12 ms, so this is affordable.

Limit: if all exchanges are already on a false plateau, multi-exchange filtering alone cannot recover the truth.

## Current Filter Assumption

The best offline candidate from the static plateau capture was a sticky EWMA tracker:

- Input: firmware median output.
- Innovation gate: +/- 0.12 m around the current estimate.
- EWMA alpha: 0.05.
- Offline result on valid rows: about 1.05 cm std, 1.72 cm span95, and 0 jumps >20 cm.

Interpretation:

- Good for `dist_smooth`, display, and static precision.
- Not sufficient alone for airbag trigger logic.
- Future `safety` output should combine fast distance, relative speed, radio quality, and N-of-M confirmation.

## 50 cm `dist_smooth` Validation

Capture: `experiments/uwb-static-50cm-20260615-dist-smooth.csv`.

Setup: two boxes static at nominal 50 cm after flashing the GenA initiator with the 30-column `dist_smooth` firmware.

Summary:

- 24,654 samples over 59.9 s.
- 24,534 firmware-valid rows.
- Raw stream around 411.6 Hz.
- `dist_smooth` was the best ranked filter in the offline sweep.

Filter comparison on this capture:

| Signal | Mean m | Std cm | Span95 cm | Max jump cm | Jumps >10 cm | Jumps >20 cm | Est. latency ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `dist` raw | 0.567 | 7.34 | 21.00 | 59.00 | 2,200 | 609 | 0.0 |
| `dist_filt` median-of-5 | 0.561 | 6.12 | 15.00 | 28.00 | 62 | 9 | 4.9 |
| `dist_smooth` sticky EWMA | 0.559 | 4.40 | 13.00 | 1.00 | 0 | 0 | 48.6 |

5 s window behavior:

- Some windows are very stable after smoothing, with `dist_smooth` std below 1 cm.
- Full-capture std remains higher because the mean slowly moves from about 0.60 m early in the capture toward about 0.52 m late in the capture.
- This looks less like isolated spike noise and more like a combination of short spikes plus slow path-regime/environment drift.

Quality correlation from valid raw samples:

| NLOS bin | Count | Mean m | Std cm | P05 m | P95 m |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 114 | 0.450 | 3.51 | 0.390 | 0.510 |
| 5 | 6,053 | 0.513 | 5.47 | 0.430 | 0.590 |
| 6 | 9,874 | 0.562 | 5.08 | 0.490 | 0.650 |
| 7 | 8,485 | 0.613 | 7.74 | 0.540 | 0.750 |

Interpretation:

- `dist_smooth` is now validated as a useful display/static precision output at 50 cm.
- The remaining bias/drift is correlated with radio-quality regime, especially NLOS, so RF profile experiments are still necessary.
- Do not use `dist_smooth` alone for safety triggering; its stickiness is useful for display but intentionally masks abrupt changes.

## Proposed RF Experiment Matrix

The plateau suggests that post-filtering is not enough. We should test RF profiles that improve first-path detection at the source.

Candidate matrix:

- CH5 / 6M8 / PLEN128 / STS off: current fast baseline.
- CH9 / 6M8 / PLEN128 / STS off.
- CH5 / 6M8 / PLEN256 / STS off.
- CH9 / 6M8 / PLEN256 / STS off.
- CH5 / 6M8 / PLEN256 / STS on.
- CH9 / 6M8 / PLEN256 / STS on.
- CH5 / 850K / PLEN1024 / STS off.
- CH9 / 850K / PLEN1024 / STS off if supported cleanly.

## RF Sweep At Nominal 50 cm

Capture date: 2026-06-15.

This sweep was run after adding UART commands on the initiator to request coordinated runtime profile changes:

- `CFG,GET_PROFILE`
- `CFG,PROFILE,<opt>`
- `CFG,CHANNEL,<5|9>`
- `CFG,RATE,<6800|850>`

Clean capture files:

- `experiments/uwb-rf-35-ch5_6m8_plen128-20260615-clean.csv`
- `experiments/uwb-rf-36-ch9_6m8_plen128-20260615-clean.csv`
- `experiments/uwb-rf-sweep-20260615-clean-summary.csv`

Summary table:

| Opt | Profile | State | Signal | Samples | Valid % | Hz | Mean m | Std cm | Span95 cm | Max jump cm | >10 cm | >20 cm | NLOS mean | Peak/FP mean | RX-FP gap dB |
| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 35 | CH5 / 6M8 / PLEN128 | ok | raw | 24,535 | 100.0 | 409.6 | 0.6367 | 2.87 | 9.00 | 15.00 | 104 | 0 | 7.00 | 1.55 | 0.90 |
| 35 | CH5 / 6M8 / PLEN128 | ok | `dist_filt` | 24,535 | 100.0 | 409.6 | 0.6387 | 2.23 | 7.00 | 7.00 | 0 | 0 | 7.00 | 1.55 | 0.90 |
| 35 | CH5 / 6M8 / PLEN128 | ok | `dist_smooth` | 24,535 | 100.0 | 409.6 | 0.6367 | 1.83 | 5.00 | 1.00 | 0 | 0 | 7.00 | 1.55 | 0.90 |
| 36 | CH9 / 6M8 / PLEN128 | ok | raw | 24,268 | 100.0 | 405.1 | 0.5490 | 5.04 | 18.00 | 18.00 | 251 | 0 | 4.32 | 4.56 | 6.17 |
| 36 | CH9 / 6M8 / PLEN128 | ok | `dist_filt` | 24,268 | 100.0 | 405.1 | 0.5483 | 4.46 | 17.00 | 8.00 | 0 | 0 | 4.32 | 4.56 | 6.17 |
| 36 | CH9 / 6M8 / PLEN128 | ok | `dist_smooth` | 24,268 | 100.0 | 405.1 | 0.5502 | 4.23 | 16.00 | 1.00 | 0 | 0 | 4.32 | 4.56 | 6.17 |
| 40 | CH5 / 850K / PLEN1024 | no samples after switch | raw | 0 | 0.0 | 0.0 | n/a | n/a | n/a | n/a | 0 | 0 | n/a | n/a | n/a |

Interpretation:

- In this placement, CH5 / 6M8 / PLEN128 clearly beats CH9 / 6M8 / PLEN128 on variation: `dist_smooth` std 1.83 cm vs 4.23 cm, span95 5 cm vs 16 cm.
- CH9 had lower NLOS score, but worse distance stability. Lower NLOS alone is therefore not enough as a quality decision rule.
- CH5 had very small RX-FP gap and low peak-to-FP, yet the distance was more stable here. The radio metrics remain context-dependent and should be evaluated together with temporal stability.
- Profile 40 did not produce samples after the switch in this runtime setup. It should be treated as unsupported until the 850K responder/initiator timing path is debugged with a dedicated firmware session.
- `dist_smooth` removed visible jumps in both working RF profiles, but it cannot fix broader RF-regime variance: CH9 remained wider even after smoothing.

## Preamble Sweep From Best CH5 Setup

Capture date: 2026-06-15.

This sweep kept the best RF family from the previous experiment constant and varied only the preamble/PAC pair:

- Channel: CH5.
- Data rate: 6.8 Mbps.
- STS: off.
- Variable: preamble length and matching PAC.

New runtime profiles added for this sweep:

- Opt 37: CH5 / 6M8 / PLEN256 / PAC16.
- Opt 38: CH5 / 6M8 / PLEN512 / PAC32.
- Opt 39: CH5 / 6M8 / PLEN1024 / PAC32.

Clean capture files:

- `experiments/uwb-preamble-35-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-preamble-37-ch5_6m8_plen256_pac16-20260615.csv`
- `experiments/uwb-preamble-38-ch5_6m8_plen512_pac32-20260615.csv`
- `experiments/uwb-preamble-sweep-20260615-summary.csv`

Summary table:

| Opt | Profile | State | Signal | Samples | Valid % | Hz | Mean m | Std cm | Span95 cm | Max jump cm | >10 cm | >20 cm | NLOS mean | Peak/FP mean | RX-FP gap dB |
| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 35 | CH5 / 6M8 / PLEN128 / PAC8 | ok | raw | 24,776 | 100.0 | 413.6 | 0.6051 | 2.94 | 9.00 | 17.00 | 345 | 0 | 6.99 | 1.60 | 0.73 |
| 35 | CH5 / 6M8 / PLEN128 / PAC8 | ok | `dist_filt` | 24,776 | 100.0 | 413.6 | 0.6044 | 1.65 | 5.00 | 7.00 | 0 | 0 | 6.99 | 1.60 | 0.73 |
| 35 | CH5 / 6M8 / PLEN128 / PAC8 | ok | `dist_smooth` | 24,776 | 100.0 | 413.6 | 0.6045 | 0.68 | 2.00 | 1.00 | 0 | 0 | 6.99 | 1.60 | 0.73 |
| 37 | CH5 / 6M8 / PLEN256 / PAC16 | ok | raw | 24,685 | 100.0 | 412.1 | 0.5915 | 3.86 | 12.00 | 18.00 | 1,121 | 0 | 6.99 | 1.63 | 0.81 |
| 37 | CH5 / 6M8 / PLEN256 / PAC16 | ok | `dist_filt` | 24,685 | 100.0 | 412.1 | 0.5916 | 2.38 | 8.00 | 10.00 | 0 | 0 | 6.99 | 1.63 | 0.81 |
| 37 | CH5 / 6M8 / PLEN256 / PAC16 | ok | `dist_smooth` | 24,685 | 100.0 | 412.1 | 0.5925 | 1.00 | 3.00 | 1.00 | 0 | 0 | 6.99 | 1.63 | 0.81 |
| 38 | CH5 / 6M8 / PLEN512 / PAC32 | ok | raw | 3,717 | 100.0 | 62.1 | 0.5754 | 4.34 | 14.00 | 24.00 | 320 | 5 | 6.94 | 1.81 | 1.08 |
| 38 | CH5 / 6M8 / PLEN512 / PAC32 | ok | `dist_filt` | 3,717 | 100.0 | 62.1 | 0.5748 | 2.46 | 9.00 | 12.00 | 2 | 0 | 6.94 | 1.81 | 1.08 |
| 38 | CH5 / 6M8 / PLEN512 / PAC32 | ok | `dist_smooth` | 3,717 | 100.0 | 62.1 | 0.5764 | 0.83 | 3.00 | 1.00 | 0 | 0 | 6.94 | 1.81 | 1.08 |
| 39 | CH5 / 6M8 / PLEN1024 / PAC32 | no samples after switch | raw | 0 | 0.0 | 0.0 | n/a | n/a | n/a | n/a | 0 | 0 | n/a | n/a | n/a |

Interpretation:

- PLEN128/PAC8 remains the best profile in this placement: highest rate and best `dist_smooth` stability, about 0.68 cm std and 2 cm span95.
- PLEN256/PAC16 kept the same high rate, but worsened stability: `dist_smooth` std 1.00 cm and span95 3 cm. It is still usable, but not better than baseline here.
- PLEN512/PAC32 was functional, but the effective rate fell to about 62 Hz and raw stability got worse. It is not attractive for the high-rate use case unless a later timing tuning recovers Hz.
- PLEN1024/PAC32 produced no samples after the runtime switch. Like the 850K profile, it needs dedicated timing/debug work before it can be evaluated fairly.
- In this bench geometry, a longer preamble did not improve first-path stability. The next more interesting RF knob is likely TX power or STS, but TX power should be tested carefully at short range because it can strengthen reflections as well as the direct path.

## TX Power Sweep From CH5 Baseline

Firmware support added on 2026-06-15:

- Initiator command: `CFG,GET_TXPWR` returns the active relative level and register value.
- Initiator command: `CFG,TXPWR,<0..4>` applies the level locally and advertises it in the Poll frame.
- Responder follows the level announced by the initiator Poll frame before transmitting its Response.
- Distance filters are reset on TX power/profile changes so `dist_filt` and `dist_smooth` are not contaminated by the previous plateau.

Confirmed sweep files:

- `experiments/uwb-txpower-l0-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l1-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l2-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l3-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l4-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-sweep-20260615-summary.csv`

Result at the current placement, CH5 / 6M8 / PLEN128 / PAC8, about 30 s per level:

| Level | Register | Hz | Raw std | Raw span95 | Raw >10 cm | Smooth std | Smooth span95 | Smooth max jump | RX dBm | FP dBm |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | `0x7f7f7f7f` | 386.1 | 2.44 cm | 7 cm | 39 | 0.70 cm | 2 cm | 1 cm | -68.3 | -70.6 |
| 1 | `0x9f9f9f9f` | 386.1 | 2.92 cm | 9 cm | 97 | 1.47 cm | 3 cm | 1 cm | -67.4 | -69.2 |
| 2 | `0xbfbfbfbf` | 386.0 | 2.84 cm | 9 cm | 133 | 0.90 cm | 3 cm | 1 cm | -65.9 | -67.7 |
| 3 | `0xdfdfdfdf` | 386.1 | 3.51 cm | 11 cm | 172 | 1.48 cm | 4 cm | 1 cm | -65.0 | -66.6 |
| 4 | `0xfdfdfdfd` | 385.8 | 2.86 cm | 10 cm | 44 | 1.19 cm | 3 cm | 2 cm | -76.0 | -79.8 |

Interpretation:

- Lower TX power did not cost update rate in this bench setup.
- Level 0 was the cleanest static setting in this capture: lowest raw std/span95 and lowest `dist_smooth` std/span95.
- Level 3 was worst here, with the most raw jumps and the largest raw span.
- Default level 4 was not the best for short-range static precision in this placement.
- Because the measured mean distance shifts between levels, repeat at 50 cm and 1 m with fixed orientation before using this as a product default.

For each profile, compare:

- Effective Hz.
- Valid percentage.
- Standard deviation and span95.
- Jumps >10 cm / >20 cm.
- Plateau count/duration.
- `nlos`, `peak_to_fp`, `rx_power - fp_power`, `fp_conf`, `clock_ppm`.
