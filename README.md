# UWB Ranging Firmware Architecture (SS-TWR)

This repository contains embedded firmware for a two-board UWB ranging system based on DW3000 + nRF52, plus the Android USB receiver app used to display and record the live distance stream.

The delivery intentionally keeps a single public README and a single supported runtime mode. The project is documented as a technical architecture guide, not as a beginner C tutorial.

For current GenA/legacy firmware variants, accelerometer targets, pyro pins, and flash commands, see `docs/ai-handoff.md`.

## 1) Current Runtime Scope

Current production-oriented behavior:

- SS-TWR is the only supported live workflow
- distance is computed on the initiator
- responder provides delayed response timestamps
- initiator outputs CSV on UART at 460800 baud
- radio runtime is fixed to channel 5 at 6.8 Mbps
- Android does not switch UWB mode, channel, data rate, or profile

Important note:

- `uwb_profiles.c` still contains legacy profile definitions that were useful during testing.
- The delivered firmware and Android app use the fixed high-rate SS-TWR path: channel 5, 6.8 Mbps, `ms,sample,dist,iax,iay,iaz,rax,ray,raz`.
- Runtime reconfiguration commands are intentionally disabled; UART command support is read-only apart from role detection.

## 2) Firmware Architecture Map

Main code areas:

- `src/main.c`: role entrypoint selection (`UWB_ROLE_INITIATOR` or `UWB_ROLE_RESPONDER`)
- `src/initiator/main_initiator.c`: initiator ranging loop, SS distance computation, CSV output
- `src/responder/main_responder.c`: responder RX/TX scheduling and timestamp embedding
- `src/common/ranging.h`: shared protocol constants, timing macros, and frame field indices
- `src/common/uwb_profiles.c`: radio profile definitions; the delivered runtime uses channel 5 / 6.8 Mbps
- `src/common/uwb_profiles.h`: runtime profile structure and API
- `src/uart/uart_log.c`: UART TX/RX implementation at 460800 baud
- `src/accel/*`: LIS2DH12 access; currently kept on TWIM1 to avoid conflicting with UARTE0
- `src/platform/*` and `src/board/*`: board-level hardware adaptation

Build-level structure:

- `CMakePresets.json`: role-specific build presets
- `CMakeLists.txt`: top-level firmware build
- `vendor/sdk`: vendored platform and DW3 SDK components
- `android-receiver-app`: Android USB receiver and CSV recorder

## 3) Where the SS-TWR Logic Lives

### 3.1 Initiator side: distance computation

Primary file:

- `src/initiator/main_initiator.c`

Key SS-TWR path:

1. Send Poll frame.
2. Receive Response frame.
3. Read responder timestamps from the response payload.
4. Compute round-trip and responder reply delay.
5. Apply DW3000 clock-offset correction.
6. Convert time of flight to meters.
7. Emit CSV (`ms,sample,dist,iax,iay,iaz,rax,ray,raz`).

The hot-path output function is `write_distance_csv(...)`.

The initiator also answers `CFG,GET_ROLE` and `INFO?` with `ROLE,INITIATOR` so the Android app can identify which board is connected over USB. Other runtime configuration commands return `ERR,READ_ONLY_SS_TWR`.

### 3.2 Responder side: timestamped delayed response

Primary file:

- `src/responder/main_responder.c`

Key SS-TWR responder path:

1. Receive Poll frame.
2. Capture `poll_rx_ts`.
3. Schedule delayed Response TX using `responder_ss_poll_rx_to_resp_tx_dly_uus`.
4. Embed `poll_rx_ts` and `resp_tx_ts` in the response.
5. Send delayed Response.

The responder also answers `CFG,GET_ROLE` and `INFO?` with `ROLE,RESPONDER`. Runtime reconfiguration commands are disabled here too.

## 4) Profile System and Fixed Delivery Profile

Profile definitions are centralized in:

- `src/common/uwb_profiles.c`

The static `profiles[]` table defines per-profile:

- PHY config (`dwt_config_t`)
- initiator and responder timing delays/timeouts
- preamble timeout
- nominal data-rate field (`data_rate_kbps`)

Defined profile options currently present in this table:

- `UWB_PROFILE_OPT_6M8_STABLE_CH5`
- `UWB_PROFILE_OPT_6M8_STABLE_CH9`
- `UWB_PROFILE_OPT_850K_ROBUST`

Delivery behavior is fixed to:

- `UWB_PROFILE_OPT_6M8_STABLE_CH5`
- channel 5
- `DWT_BR_6M8`
- preamble length 128
- PAC 8
- STS off

### 4.1 About the legacy robust profile

`UWB_PROFILE_OPT_850K_ROBUST` is still defined in the code because it was useful during earlier robustness experiments. It contains:

- `DWT_BR_850K`
- `DWT_PLEN_1024`
- `DWT_PAC32`
- longer RX/TX delays and timeouts

It is not exposed by the Android app and is not part of the supported delivery workflow.

## 5) Runtime Mode vs Radio Profile

There are two concept layers that are easy to confuse:

1. Ranging mode:

- delivered value: SS-TWR only
- encoded in the Poll control byte as `RANGING_MODE_SS_TWR`
- used to select the two-message SS-TWR distance path

2. PHY/runtime radio profile:

- delivered value: channel 5 / 6.8 Mbps
- defined by `UWB_PROFILE_OPT_6M8_STABLE_CH5`
- used to configure DW3000 PHY and timing values

The Android app intentionally does not expose either layer as a setting. For delivery, these are firmware constants, not user controls.

## 6) Key Timing Constants

Global shared timing constants are in:

- `src/common/ranging.h`

Important constants used by the active SS-TWR path:

- `POLL_TX_TO_RESP_RX_DLY_UUS`
- `SS_POLL_RX_TO_RESP_TX_DLY_UUS`
- `RESP_RX_TIMEOUT_UUS`
- `RNG_DELAY_MS`

These constants feed the profile timing in `uwb_profiles.c` and shape throughput/stability tradeoffs.

## 7) Practical Ranging Parameters

This section maps important parameters to where they are used and what behavior they control.

### 7.1 `POLL_TX_TO_RESP_RX_DLY_UUS`

- Defined in `src/common/ranging.h`
- Applied on initiator RX scheduling after Poll TX
- Purpose: wait a short, deterministic gap before listening for Response

### 7.2 `SS_POLL_RX_TO_RESP_TX_DLY_UUS`

- Defined in `src/common/ranging.h`
- Applied on responder delayed Response TX
- Purpose: give responder enough time to process Poll and schedule a valid delayed TX

### 7.3 `RESP_RX_TIMEOUT_UUS`

- Defined in `src/common/ranging.h`
- Applied on initiator response wait window
- Purpose: stop waiting when Response is missing or late and keep the loop moving

### 7.4 `RNG_DELAY_MS`

- Defined in `src/common/ranging.h`
- Applied as loop pacing delay unless the turbo path runs back-to-back
- Purpose: control global ranging pressure across CPU, radio, and UART

## 8) Why `SS_POLL_RX_TO_RESP_TX_DLY_UUS` Is Not Zero

In theory, responding immediately sounds faster. In practice, a non-zero delay is required for robust timing:

- responder still needs finite time after Poll RX to prepare and schedule Response
- delayed TX must be programmed early enough to be valid
- too little margin increases late-TX failures and timestamp instability

So this delay is a stability margin, not just overhead.

## 9) What Happens If `RNG_DELAY_MS = 0`

Setting `RNG_DELAY_MS` to zero usually means:

- maximum loop pressure with back-to-back cycles
- higher average throughput potential
- higher timeout probability and burst losses
- higher jitter and less stable distance under real conditions

It can look fast on short runs but often degrades stability during sustained operation. The current high-rate firmware keeps the validated timing path that was tested with the Android receiver.

## 10) Why Frame Losses Happen

Typical loss mechanisms in this code path:

- RX window mismatch: Response arrives outside the initiator RX timeout window
- delayed TX miss: responder cannot schedule/send Response in time
- preamble detection miss: channel conditions such as NLOS, multipath, or interference delay or corrupt detection
- system pressure: loop rate, UART output, and radio timing become too aggressive together

Rule of thumb:

- reducing delays increases speed and risk
- increasing delays/timeouts improves robustness and lowers peak rate

## 11) Worked SS-TWR Exchange Example

Below is a simplified SS-TWR exchange with made-up timestamps to show the math clearly.

### 11.1 Frame sequence

```text
Initiator                           Responder
   | -- Poll ----------------------> |
   |                                 |
   | <---- Response ---------------- |
        |      poll_rx_ts, resp_tx_ts,    |
        |      responder accel XYZ        |
   |                                 |
   | compute ToF and distance        |
        | output CSV with distance + accel|
```

### 11.2 Dummy timestamps

- Initiator Poll TX timestamp: `poll_tx_ts = 1,000,000`
- Responder Poll RX timestamp: `responder_poll_rx_ts = 1,000,600`
- Responder Response TX timestamp: `responder_resp_tx_ts = 1,001,500`
- Initiator Response RX timestamp: `resp_rx_ts = 1,002,200`

### 11.3 Intermediate terms

- `rtd_init = resp_rx_ts - poll_tx_ts = 2,200`
- `reply_resp = responder_resp_tx_ts - responder_poll_rx_ts = 900`

Assume a small clock offset ratio from DW3000 readback:

- `clock_offset_ratio = 0.00002`

SS-TWR ToF estimate used by the initiator:

```text
tof_dtu = (rtd_init - reply_resp * (1 - clock_offset_ratio)) / 2
        = (2200 - 900 * 0.99998) / 2
        = (2200 - 899.982) / 2
        ~= 650.009 dtu
```

Distance conversion:

```text
distance_m = tof_dtu * DWT_TIME_UNITS * SPEED_OF_LIGHT
```

With the constants used by the SDK, this dummy example lands around a few meters. The numbers are only illustrative.

### 11.4 Example CSV line

```text
1203,57,3.00,18,-42,1001,12,-39,998
```

Meaning:

- `1203`: local initiator milliseconds
- `57`: measurement counter
- `3.00`: computed distance in meters
- `18,-42,1001`: initiator accelerometer X/Y/Z in mg
- `12,-39,998`: responder accelerometer X/Y/Z in mg

## 12) UART Contract

Current primary runtime CSV output:

```text
ms,sample,dist,iax,iay,iaz,rax,ray,raz
```

Fields:

- `ms`: local initiator milliseconds
- `sample`: measurement counter
- `dist`: computed distance in meters
- `iax,iay,iaz`: initiator accelerometer X/Y/Z in mg
- `rax,ray,raz`: responder accelerometer X/Y/Z in mg transported over UWB Response

UART configuration:

- firmware side configured in `src/uart/uart_log.c`
- current operating baud is 460800
- format is 8N1, no flow control

Supported app-facing UART commands:

- `CFG,GET_ROLE`
- `INFO?`

Both return the board role. Other configuration commands are intentionally rejected in this delivery build.

## 13) Android App

The companion Android receiver app is in:

- `android-receiver-app/`

Runtime behavior:

- connects to the initiator over USB OTG serial
- uses 460800 baud
- parses `ms,sample,dist,iax,iay,iaz,rax,ray,raz`
- displays live distance, quality indicators, and session status
- records CSV files to Android Downloads
- does not send runtime UWB mode/channel/profile commands

Build from the app directory:

```bash
cd android-receiver-app
./gradlew assembleDebug
```

Install the debug APK:

```bash
adb install -r android-receiver-app/app/build/outputs/apk/debug/app-debug.apk
```

## 14) Build and Flash

Build from repository root:

```bash
cmake --preset=initiator_debug
cmake --build --preset initiator_debug

cmake --preset=responder_debug
cmake --build --preset responder_debug
```

Typical output files:

- `build/initiator_debug/uwb_initiator.hex`
- `build/responder_debug/uwb_responder.hex`

Flash examples:

```bash
nrfjprog --snr <INITIATOR_SNR> --program build/initiator_debug/uwb_initiator.hex --chiperase --verify --reset
nrfjprog --snr <RESPONDER_SNR> --program build/responder_debug/uwb_responder.hex --chiperase --verify --reset
```

When multiple J-Link probes are connected, pass `--snr` to avoid the probe selection dialog.

## 15) Quick Code Reading Order

For a fast architecture walkthrough, read these files in order:

1. `src/main.c`
2. `src/common/ranging.h`
3. `src/common/uwb_profiles.c`
4. `src/initiator/main_initiator.c`
5. `src/responder/main_responder.c`
6. `src/uart/uart_log.c`
7. `android-receiver-app/app/src/main/java/com/qorvo/uwbreceiver/service/UwbForegroundService.kt`

This order gives role selection, timing/profile model, SS ranging logic, UART output, and Android acquisition flow.
