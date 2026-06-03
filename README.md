# UWB Ranging

This repository contains the validated two-board UWB ranging firmware and its Android USB receiver app.

## Runtime

There is one supported runtime mode:
- ranging mode: SS-TWR
- radio profile: channel 5, 6.8 Mbps
- distance computation: initiator side
- responder behavior: delayed response with embedded timestamps
- serial output: `460800` baud, 8N1
- CSV payload: `ms,sample,dist`

The Android app does not switch radio mode, channel, or profile. It connects to the initiator over USB OTG serial, displays the live distance stream, and records CSV files.

## Firmware Layout

Main files:
- `src/main.c`: selects initiator or responder build role
- `src/initiator/main_initiator.c`: sends Poll frames, receives Responses, computes distance, emits CSV
- `src/responder/main_responder.c`: receives Poll frames and sends delayed timestamped Responses
- `src/common/ranging.h`: shared protocol constants and frame field indices
- `src/common/uwb_profiles.c`: fixed channel 5 / 6.8 Mbps radio configuration used by this build
- `src/uart/uart_log.c`: USB serial output at `460800` baud

## SS-TWR Exchange

```text
Initiator                           Responder
   | -- Poll ----------------------> |
   |                                 |
   | <---- Response ---------------- |
   |      poll_rx_ts, resp_tx_ts     |
   |                                 |
   | compute distance               |
   | output ms,sample,dist          |
```

The initiator computes time of flight from:
- local Poll TX timestamp
- local Response RX timestamp
- responder Poll RX timestamp
- responder Response TX timestamp

It then applies DW3000 clock-offset correction and converts the result to meters.

## CSV Contract

Default firmware output:

```text
ms,sample,dist
```

Example:

```text
1203,57,3.00
```

Fields:
- `ms`: initiator local timestamp in milliseconds
- `sample`: measurement counter
- `dist`: distance in meters

The firmware also accepts `CFG,GET_ROLE`/`INFO?` over UART so the Android app can identify whether the connected USB board is the initiator or responder. Runtime reconfiguration commands are intentionally not part of the supported interface.

## Android App

The companion app is in `android-receiver-app/`.

It is fixed for the validated SS-TWR stream:
- serial baud: `460800`
- expected firmware CSV: `ms,sample,dist`
- USB connection target: initiator board for live distance samples
- output: CSV recording in Android Downloads

Build from the app directory:

```bash
cd android-receiver-app
./gradlew assembleDebug
```

## Firmware Build and Flash

Build from repository root:

```bash
cmake --preset=initiator_debug
cmake --build --preset initiator_debug

cmake --preset=responder_debug
cmake --build --preset responder_debug
```

Output files:
- `build/initiator_debug/uwb_initiator.hex`
- `build/responder_debug/uwb_responder.hex`

Flash examples:

```bash
nrfjprog --snr <INITIATOR_SNR> --program build/initiator_debug/uwb_initiator.hex --sectorerase --verify --reset
nrfjprog --snr <RESPONDER_SNR> --program build/responder_debug/uwb_responder.hex --sectorerase --verify --reset
```

When multiple J-Link probes are connected, pass `--snr` to avoid the probe selection dialog.
