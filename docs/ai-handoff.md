# AI Handoff - UWB Ranging Variants

This repository builds firmware for DWM3001CDK-based UWB ranging boxes and an Android USB receiver app.

The important current rule is: do not rely on runtime accelerometer auto-detection for field boxes. Build deterministic variants instead.

## Product Context

We are developing an embedded fall-detection system for equestrian airbag use. The goal is to measure the real-time relative distance between rider and horse using UWB (Ultra-Wideband), with a very high measurement rate around 100 Hz or more and minimal latency.

These distance measurements are combined with accelerometer, gyroscope, and other inertial sensor data to detect fall situations as early as possible and trigger the airbag before impact. The main challenge is to obtain robust, reliable distance measurements despite fast movement, vibration, body masking, and the power constraints of a wearable system.

See `docs/target.md` for the target product direction and the planned continuous motion-energy signals from UWB, accelerometer, and gyroscope data.

## Firmware Variants

Variants are selected with CMake presets through `UWB_ROLE` and `UWB_ACCEL_TARGET`.

| Preset | Role | Accelerometer | Pyro trigger |
| --- | --- | --- | --- |
| `initiator_legacy_debug` | Initiator | LIS2DH12 only | Relays `FIRE` over UWB |
| `initiator_gena_debug` | Initiator | BMI323 accel + gyro | Relays `FIRE` over UWB |
| `responder_legacy_debug` | Responder | LIS2DH12 only | Disabled, returns `ERR,PYRO_UNAVAILABLE` |
| `responder_gena_debug` | Responder | BMI323 accel + gyro | Enabled |

The old/default `initiator_debug` and `responder_debug` presets currently target `legacy`.

`UWB_ACCEL_TARGET=auto` still exists in `src/accel/accel.c`, but do not use it for mixed legacy/GenA fleet validation unless the user explicitly asks for probing behavior.

## Build Commands

Run from the repository root:

```sh
cmake --preset initiator_legacy_debug
cmake --build --preset initiator_legacy_debug

cmake --preset responder_gena_debug
cmake --build --preset responder_gena_debug

cmake --preset responder_legacy_debug
cmake --build --preset responder_legacy_debug
```

Generated debug hex names include the target to avoid flashing the wrong generation:

```text
build/initiator_legacy_debug/uwb_initiator_legacy.hex
build/initiator_gena_debug/uwb_initiator_gena.hex
build/responder_legacy_debug/uwb_responder_legacy.hex
build/responder_gena_debug/uwb_responder_gena.hex
```

## Flash Commands Used During 2026-06-09 Validation

Known probes from the session:

| Probe | Last flashed as |
| --- | --- |
| `760221448` | Initiator legacy |
| `802009545` | Responder GenA |
| `760220908` | Responder legacy |

Examples:

```sh
nrfjprog --program build/initiator_legacy_debug/uwb_initiator_legacy.hex --sectorerase --verify -f NRF52 --snr 760221448 && nrfjprog --reset -f NRF52 --snr 760221448

nrfjprog --program build/responder_gena_debug/uwb_responder_gena.hex --sectorerase --verify -f NRF52 --snr 802009545 && nrfjprog --reset -f NRF52 --snr 802009545

nrfjprog --program build/responder_legacy_debug/uwb_responder_legacy.hex --sectorerase --verify -f NRF52 --snr 760220908 && nrfjprog --reset -f NRF52 --snr 760220908
```

`nrfjprog` often prints `JLinkARM.dll reported error -256` noise on this machine, but programming has still succeeded when the progress log ends with `Verify successful` and `Applying system reset. Run.`

## GenA Initiator UART POC (2026-06-15)

New-generation DWM3001C box was validated as an initiator over an external USB-UART adapter:

- Firmware: `initiator_gena_debug` → `build/initiator_gena_debug/uwb_initiator_gena.hex`
- Probe used: `802009545`
- UART pins: board TX `P0.19`, board RX `P0.15`
- Mac serial port during test: `/dev/cu.usbserial-0001` at 460800 8N1
- Flash result: `Write successful`, `Verify successful`, `Run`
- Live CSV: 29 columns at ~429 Hz, `dist_filt` stable around 0.5 m in the bench setup
- Command RX: fast `CFG,GET_ROLE` and `INFO?` both returned `ROLE,INITIATOR`

Follow-up after reconnecting the nRF/debug path later on 2026-06-15:

- SWD access recovered on probe `802009545`: `nrfjprog --memrd 0x10000060 --n 4 -f NRF52 --snr 802009545` returned `0x850DAB67`.
- `initiator_gena_debug` with `dist_smooth` was flashed successfully to `build/initiator_gena_debug/uwb_initiator_gena.hex`.
- Success markers: `Write successful`, `Verify successful`, `Applying system reset. Run.`
- Live UART now emits 30 columns: `... valid,dist_filt,dist_smooth`.
- Commands remained healthy after flash: `CFG,GET_ROLE` and `INFO?` both returned `ROLE,INITIATOR` while the CSV stream was running around 411 Hz.

Important firmware note: `src/uart/uart_log.c` now receives UART commands through a 64-byte EasyDMA RX buffer flushed from `uart_log_poll_rx()`. The previous one-byte polling RX could only handle slow character-by-character input; phone/host commands sent at full 460800 baud were corrupted or returned `ERR,READ_ONLY_SS_TWR`.

## Android Armed Trigger Buttons

The Android app has two one-shot armed trigger buttons in the Controls card:

- `distance-armed-2m`: arms a distance trigger. On the first sample where the app display distance is at least 2.00 m, the phone buzzes, sends `PYRO,FIRE`, and immediately disarms.
- `tilt-armed-50°`: captures the receiver accelerometer pitch/roll as the baseline at arm time. On the first sample where receiver pitch or roll differs by at least 50 degrees from that baseline, the phone buzzes, sends `PYRO,FIRE`, and immediately disarms.

Implementation notes:

- Actions are handled in `UwbForegroundService` as `ACTION_ARM_DISTANCE_2M` and `ACTION_ARM_TILT_50_DEG`.
- The distance trigger uses `dist_smooth` when firmware provides it, then `dist_filt`, then raw `dist` as fallback.
- The tilt trigger uses receiver accelerometer fields `rax/ray/raz`, not phone IMU orientation.
- `RuntimeState.safetyArmMode` and `RuntimeState.safetyArmStatus` are displayed in Session status.
- Manual `FIRE` remains available and still sends `PYRO,FIRE`.

## Static Precision Experiment (2026-06-15)

Detailed radio-quality vocabulary, plateau interpretation, and filter assumptions are documented in `docs/quality-assumptions.md`.

Capture file: `experiments/uwb-static-1m-20260615-101332.csv`.

- Setup: two boxes nominally static around 1 m; absolute distance is not the target, variation is.
- Capture: 25,414 rows over 59.9 s, ~424 Hz raw stream, 18,610 valid rows.
- Observation: this is not simple Gaussian noise. The stream stayed around ~1.74 m for ~15 s, then switched into a false plateau near ~2.8 m. Classical rolling means/medians smoothed the false plateau instead of rejecting it.
- Best offline candidate for a high-precision/display channel: sticky EWMA tracker on the firmware median distance, innovation gate ±0.12 m, alpha 0.05. Offline result on valid rows: ~1.05 cm std, ~1.72 cm span95, 0 jumps >20 cm, while holding through the false plateau.
- Firmware change prepared: `dist_smooth` appended after `dist_filt` in the initiator CSV. `dist_filt` remains the fast median-of-5 output; `dist_smooth` is the slow/sticky precision output and should not replace the future airbag fast path without a separate dynamic test.
- Build status: `cmake --build --preset initiator_gena_debug` succeeded after adding `dist_smooth`.
- Flash status: done after reconnecting the nRF/debug path. If SWD fails again, first ask the user to disconnect/reconnect or power-cycle before retrying; `nrfjprog --ids` can see the probe even when target SWD is not usable.

### 50 cm validation with `dist_smooth`

Capture file: `experiments/uwb-static-50cm-20260615-dist-smooth.csv`.

- Setup: two boxes static at nominal 50 cm. Absolute calibration is still not the goal; stability and spike rejection are.
- Capture: 24,654 samples over 59.9 s, 24,534 valid rows, ~411.6 Hz.
- CSV format: 30 columns, including `dist_smooth` after `dist_filt`.
- Raw distance: mean 0.567 m, std 7.34 cm, span95 21 cm, max jump 59 cm, 2,200 jumps >10 cm, 609 jumps >20 cm.
- Firmware median-of-5: mean 0.561 m, std 6.12 cm, span95 15 cm, max jump 28 cm, 62 jumps >10 cm, 9 jumps >20 cm.
- Firmware sticky smooth: mean 0.559 m, std 4.40 cm, span95 13 cm, max jump 1 cm, 0 jumps >10 cm, 0 jumps >20 cm, estimated latency ~49 ms.
- Interpretation: at short range the raw signal still contains many accepted spikes/regime variations, but the sticky `dist_smooth` channel removes visible jumps. Several 5 s windows are very tight, while the full 60 s distribution includes slow drift/regime movement from roughly 0.60 m toward 0.52 m.
- Radio-quality correlation: higher `nlos` bins correlate with longer measured distance (`nlos=5` mean ~0.513 m, `nlos=6` ~0.562 m, `nlos=7` ~0.613 m). This supports RF/profile testing rather than relying only on post-filtering.

### RF sweep at nominal 50 cm

To run RF profile tests without reflashing between every point, the GenA initiator now accepts:

- `CFG,GET_PROFILE`
- `CFG,PROFILE,<opt>`
- `CFG,CHANNEL,<5|9>`
- `CFG,RATE,<6800|850>`

The command arms the existing Poll/Response profile-switch handshake. The responder follows the initiator when the Poll carries the pending profile and token.

Clean sweep files:

- `experiments/uwb-rf-35-ch5_6m8_plen128-20260615-clean.csv`
- `experiments/uwb-rf-36-ch9_6m8_plen128-20260615-clean.csv`
- `experiments/uwb-rf-sweep-20260615-clean-summary.csv`

Result at the current placement:

- Opt 35, CH5 / 6M8 / PLEN128: 24,535 samples, 409.6 Hz, 100 % valid. `dist_smooth`: mean 0.6367 m, std 1.83 cm, span95 5 cm, max jump 1 cm, 0 jumps >10 cm.
- Opt 36, CH9 / 6M8 / PLEN128: 24,268 samples, 405.1 Hz, 100 % valid. `dist_smooth`: mean 0.5502 m, std 4.23 cm, span95 16 cm, max jump 1 cm, 0 jumps >10 cm.
- Opt 40, CH5 / 850K / PLEN1024: no samples after runtime switch. It likely needs dedicated timing/profile debugging before it can be used in SS-TWR sweep runs.

Practical note: after trying opt 40, the system may need both boards reset/power-cycled to return to a working 35/35 baseline. After this sweep, the boards were switched back to 35/35 and live CSV was confirmed.

### Preamble sweep from CH5 baseline

Profiles added in `src/common/uwb_profiles.c` / `.h`:

- Opt 37: CH5 / 6M8 / PLEN256 / PAC16.
- Opt 38: CH5 / 6M8 / PLEN512 / PAC32.
- Opt 39: CH5 / 6M8 / PLEN1024 / PAC32.

Both initiator GenA and responder GenA were flashed with the updated profile table. Baseline was restored to 35/35 after the sweep.

Clean sweep files:

- `experiments/uwb-preamble-35-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-preamble-37-ch5_6m8_plen256_pac16-20260615.csv`
- `experiments/uwb-preamble-38-ch5_6m8_plen512_pac32-20260615.csv`
- `experiments/uwb-preamble-sweep-20260615-summary.csv`

Result at the current placement:

- Opt 35, PLEN128/PAC8: 24,776 samples, 413.6 Hz, 100 % valid. `dist_smooth`: mean 0.6045 m, std 0.68 cm, span95 2 cm, max jump 1 cm.
- Opt 37, PLEN256/PAC16: 24,685 samples, 412.1 Hz, 100 % valid. `dist_smooth`: mean 0.5925 m, std 1.00 cm, span95 3 cm, max jump 1 cm.
- Opt 38, PLEN512/PAC32: 3,717 samples, 62.1 Hz, 100 % valid. `dist_smooth`: mean 0.5764 m, std 0.83 cm, span95 3 cm, max jump 1 cm.
- Opt 39, PLEN1024/PAC32: no samples after runtime switch.

Practical conclusion: PLEN128/PAC8 remains the best high-rate profile here. PLEN256 did not improve stability, and PLEN512 costs too much rate. PLEN1024 needs timing/debug work before it can be evaluated.

### TX power sweep from CH5 baseline

Firmware support added after the preamble sweep:

- `CFG,GET_TXPWR` on the initiator returns `TXPWR,<level>,<register>`.
- `CFG,TXPWR,<0..4>` on the initiator applies a relative TX power register level.
- The initiator writes the selected level into Poll byte 29; the responder follows it before transmitting Response.
- The initiator resets gate/median/smooth filter state when TX power or runtime profile changes.

Relative CH5 levels used by the sweep:

| Level | TX_POWER register |
| ---: | --- |
| 0 | `0x7f7f7f7f` |
| 1 | `0x9f9f9f9f` |
| 2 | `0xbfbfbfbf` |
| 3 | `0xdfdfdfdf` |
| 4 | `0xfdfdfdfd` |

Level 4 is the previous/current Qorvo CH5 default. These are relative register values, not calibrated dBm settings.

Tooling:

```sh
./.venv/bin/python tools/uwb_tx_power_sweep.py --port /dev/cu.usbserial-0001 --baud 460800 --seconds 30 --settle-seconds 2 --levels 0,1,2,3,4
```

The tool confirms each level with `CFG,GET_TXPWR` before capture and restores level 4 at the end.

Clean sweep files:

- `experiments/uwb-txpower-l0-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l1-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l2-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l3-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-l4-ch5_6m8_plen128_pac8-20260615.csv`
- `experiments/uwb-txpower-sweep-20260615-summary.csv`

Result at the current placement, CH5 / 6M8 / PLEN128 / PAC8:

- Level 0, `0x7f7f7f7f`: 12,338 samples, 386.1 Hz. Raw std 2.44 cm, span95 7 cm, 39 jumps >10 cm. `dist_smooth` std 0.70 cm, span95 2 cm, max jump 1 cm.
- Level 1, `0x9f9f9f9f`: 12,337 samples, 386.1 Hz. Raw std 2.92 cm, span95 9 cm, 97 jumps >10 cm. `dist_smooth` std 1.47 cm, span95 3 cm, max jump 1 cm.
- Level 2, `0xbfbfbfbf`: 12,335 samples, 386.0 Hz. Raw std 2.84 cm, span95 9 cm, 133 jumps >10 cm. `dist_smooth` std 0.90 cm, span95 3 cm, max jump 1 cm.
- Level 3, `0xdfdfdfdf`: 12,337 samples, 386.1 Hz. Raw std 3.51 cm, span95 11 cm, 172 jumps >10 cm, 2 jumps >20 cm. `dist_smooth` std 1.48 cm, span95 4 cm, max jump 1 cm.
- Level 4, `0xfdfdfdfd`: 12,326 samples, 385.8 Hz. Raw std 2.86 cm, span95 10 cm, 44 jumps >10 cm. `dist_smooth` std 1.19 cm, span95 3 cm, max jump 2 cm.

Practical conclusion: in this short-range placement, the lowest tested TX power level 0 is best and does not reduce Hz. Retest level 0 vs level 4 at fixed 50 cm and 1 m before treating it as the new default.

## Accelerometer Pins

GenA BMI323 backend in `src/accel/accel.c` uses:

| Signal | nRF pin |
| --- | --- |
| SCLK | `P0.17` |
| MOSI | `P0.20` |
| MISO | `P0.21` |
| CS | `P0.11` |
| INT1 | `P1.8` |
| INT2 | `P0.6` |

Legacy LIS2DH12 backend uses the original DWM3001C I2C/TWI path in `src/accel/accel.c`.

## BMI323 Gyroscope Path

GenA firmware reads BMI323 gyroscope registers as raw signed 16-bit values. Legacy firmware reports `0,0,0` for gyro fields.

UWB payload additions:

| Frame | Bytes | Meaning |
| --- | --- | --- |
| Poll | `23..28` | Initiator gyro XYZ, three int16 little-endian raw values |
| Response | `31..36` | Responder gyro XYZ, three int16 little-endian raw values |

CSV additions:

```text
igx,igy,igz,rgx,rgy,rgz
```

For initiator-generated CSV, these six fields are appended after `rax,ray,raz`.

For responder-generated CSV, these six fields are appended after `resp_acq_ms,init_acq_ms,resp_profile_opt,init_profile_opt` because radio metrics and control fields are already present before them.

The Android app parses both layouts and displays two cards: `Initiator BMI gyro` and `Receiver BMI gyro`. Phone gyro remains separate under `Phone sensors` as `Phone gyro rad/s`, and the recorded CSV names it `tgx,tgy,tgz`.

## Session 2026-06-10 — Résultats mesurés et filtre médian

### Performance mesurée en live (après pipelining UART)
- Port série `/dev/cu.usbmodem0007602214481` @ 460800 : **~391 Hz** soutenu (host_mean_hz 391, fw_mean_hz 393, fw_p50 3 ms, fw_p95 3 ms). Avant : ~54 Hz. Gain ×7, obtenu uniquement par le recouvrement DMA UART / échange UWB (zéro changement RF).
- Précision statique (10 s, boîtiers immobiles, ~0.36 m) : 3914 échantillons, **std 2.6 cm**, span95 (p05–p95) 8 cm, 0 saut >20 cm, 100 % valid.
- Gate en conditions réelles : 98.8 % valid (3865 valid / 46 rejets sur 10 s pendant manipulation).

### Flash
- Initiator legacy (probe 760221448) : hex correct = `build/initiator_debug/uwb_initiator_legacy.hex`. **Piège** : `build/initiator_legacy_debug/` contient un hex périmé (9 juin) — un premier flash avec ce hex a donné un CSV 15 colonnes, détecté en live et corrigé.
- Responder GenA (probe 802009545) : build existant reflashé.
- Commande : `nrfjprog --program <hex> --sectorerase --verify -f NRF52 --snr <snr> && nrfjprog --reset -f NRF52 --snr <snr>`. Les erreurs JLinkARM `-256` sont du bruit cosmétique sur macOS.

### Filtre médian-de-5 firmware (colonne `dist_filt`)
- `main_initiator.c` : fenêtre glissante de 5 distances, alimentée **uniquement par les échantillons validés par la gate** (un burst d'outliers NLOS ne pollue pas le filtre ; les invalides gardent la dernière valeur filtrée).
- À ~390 Hz, latence ajoutée ≈ 13 ms — négligeable pour le déclenchement airbag.
- Colonne `dist_filt` (cm→m, 2 décimales) ajoutée **en fin de ligne après `valid`** (29ᵉ colonne, index 28) → le parser Android existant (valid à gyroStart+6) reste intact.
- Android : `CsvSample.firmwareDistFilt` parsé à gyroStart+7 ; `UwbForegroundService` utilise `firmwareDistFilt ?: dist` comme entrée du filtre d'affichage ; `RecordingManager` enregistre la colonne `fw_dist_filt`.
- **Validé live après reflash** (10 s, ~389 Hz, 29 colonnes) : std brut 3,18 cm → filtré **1,61 cm** (−49 %), span95 11,0 → **5,0 cm** (−55 %), 0 saut >20 cm. Débit inchangé.

## Initiator Quality Gate And Extended CSV (2026-06-10)

The SS-TWR initiator now emits the full 28-field CSV format (same layout the parser already used for responder lines, plus a trailing `valid` flag):

```text
ms,sample,dist,rx_power,fp_power,clock_ppm,score,nlos,peak_fp,fp_conf,sts,iax,iay,iaz,rax,ray,raz,resp_acq_ms,init_acq_ms,resp_profile_opt,init_profile_opt,igx,igy,igz,rgx,rgy,rgz,valid
```

Key changes:

- CIA diagnostics are enabled on the initiator (`radio_quality_enable_diagnostics()` replaces `DW_CIA_DIAG_LOG_OFF`), so `rx_power_dbm`, `fp_power_dbm`, `peak_to_fp`, `fp_conf_level` are now populated in recordings (they were empty in the 20260609 sessions).
- A physical plausibility gate (`gate_check_distance` in `src/initiator/main_initiator.c`) flags samples outside `[-0.5, 100] m` or implying relative speed above 12 m/s. Invalid samples are still logged with `valid=0`; after 8 consecutive rejections the gate re-baselines so it can never lock out a genuine fast change.
- UART TX is pipelined (`src/uart/uart_log.c`): the DMA transfer overlaps the next ranging exchange instead of blocking ~1.7 ms per line. Ping-pong buffers, CRLF in the final chunk, chunk size 192.
- The Android app parses the `valid` flag (`CsvSample.firmwareValid`), excludes invalid samples from the display median window, and records it as `fw_valid` (last CSV column).

Airbag trigger note: the FIRE path is still manual. When auto-trigger is implemented, use N-of-M confirmation (e.g. 2-of-3 consecutive `valid=1` samples beyond threshold) rather than single-sample triggering.

## Buzzer And Pyro Pins

The buzzer pin was recovered from the historical `feature/pyro-gyro-module` branch:

| Function | nRF pin | Notes |
| --- | --- | --- |
| Buzzer | `P1.5` | Used for BMI323 three-beep recognition and GenA pyro countdown |
| Pyro trigger | `P1.9` | Responder GenA only; active high |
| Countdown LED 0 | `P0.14` | Board LED, active low |
| Countdown LED 1 | `P0.22` | Board LED, active low |
| Countdown LED 2 | `P0.5` | Board LED, active low |
| Countdown LED 3 | `P0.4` | Board LED, active low |

Responder GenA behavior:

1. Receives `PYRO,FIRE` locally over USB, or receives the UWB fire relay flag from an initiator.
2. Logs `ACK,PYRO_ARMED` or `ACK,PYRO_REMOTE_ARMED`.
3. Starts a 10 second countdown with buzzer/LED feedback.
4. Sets `P1.9` high for 2 seconds.
5. Clears `P1.9` and logs `PYRO,DONE`.

Keep live ignition hardware disconnected during firmware-only validation.

## FIRE Command Path

The Android app has a `FIRE` button in `UwbMainScreen`. The service sends:

```text
PYRO,FIRE\n
```

Initiators of both generations accept `PYRO,FIRE` or `FIRE` over USB and relay the command for 200 Poll frames using Poll byte `22`.

Responder GenA reads Poll byte `22` and starts the countdown if it is idle. Responder legacy intentionally does not drive the pyro output.

This preserves the SS-TWR distance and CSV path; the fire flag is appended in the Poll control area and is not part of the distance timestamp calculation.

## Android Build And Install

Use the Android SDK env vars on this machine:

```sh
ANDROID_HOME="$HOME/Library/Android/sdk" ANDROID_SDK_ROOT="$HOME/Library/Android/sdk" ./android-receiver-app/gradlew -p android-receiver-app assembleDebug
adb install -r android-receiver-app/app/build/outputs/apk/debug/app-debug.apk
```

The APK ignores old `ACCEL_SRC` / `ACCEL,` runtime source lines so an older firmware line does not pollute the UI.

## Current Session Validation

On 2026-06-09 the following passed before commit:

```sh
cmake --build --preset responder_gena_debug
cmake --build --preset responder_legacy_debug
cmake --build --preset initiator_legacy_debug
cmake --preset initiator_gena_debug && cmake --build --preset initiator_gena_debug
ANDROID_HOME="$HOME/Library/Android/sdk" ANDROID_SDK_ROOT="$HOME/Library/Android/sdk" ./android-receiver-app/gradlew -p android-receiver-app assembleDebug
```

The APK was installed on Android device `0B021JEC206422`.

## AI Cost Tips

Use these practices to reduce LLM cost while keeping good output quality on this project.

1. Keep sessions task-scoped.
	Start a new chat for each topic (firmware build/flash, Android UI, CSV analysis). Very long chats resend a lot of old context on every prompt.

2. Give file-scoped prompts.
	Prefer: "Update `tools/csv_explorer_app.py` in `compute_fast_instability`" over broad prompts like "improve analysis". Precise prompts reduce exploration tokens.

3. Batch requests in one prompt.
	Ask for grouped work in one message when possible (edit + build + quick validation), instead of many short back-and-forth turns.

4. Reuse repo facts from this file.
	Put stable facts (pins, payload offsets, build commands, probe IDs) here so future sessions do not need repeated code archaeology.

5. Use lighter models for routine tasks.
	Renames, small patches, and command reruns do not need a premium model. Save stronger models for architecture/debug sessions.

6. Avoid multi-agent "discussion" unless work is truly parallel.
	Multiple agents debating the same issue usually multiplies token usage. Use one main agent, and only parallelize independent subtasks.

7. Save recurring analyses as scripts.
	If the same SS-TWR study is run often, keep it as a reusable script under `tools/` so follow-up runs do not require regenerated code.
