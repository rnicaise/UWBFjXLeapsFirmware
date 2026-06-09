# AI Handoff - UWB Ranging Variants

This repository builds firmware for DWM3001CDK-based UWB ranging boxes and an Android USB receiver app.

The important current rule is: do not rely on runtime accelerometer auto-detection for field boxes. Build deterministic variants instead.

## Firmware Variants

Variants are selected with CMake presets through `UWB_ROLE` and `UWB_ACCEL_TARGET`.

| Preset | Role | Accelerometer | Pyro trigger |
| --- | --- | --- | --- |
| `initiator_legacy_debug` | Initiator | LIS2DH12 only | Relays `FIRE` over UWB |
| `initiator_gena_debug` | Initiator | BMI323 only | Relays `FIRE` over UWB |
| `responder_legacy_debug` | Responder | LIS2DH12 only | Disabled, returns `ERR,PYRO_UNAVAILABLE` |
| `responder_gena_debug` | Responder | BMI323 only | Enabled |

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
