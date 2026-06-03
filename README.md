# UWB Ranging Firmware (SS-TWR)

This repository contains the embedded firmware for a two-board UWB ranging setup based on Qorvo DW3000 and nRF52.

Current runtime target:
- SS-TWR live workflow
- Initiator computes distance
- Responder returns delayed response timestamps
- UART stream from initiator at 460800 baud

For detailed implementation notes, see README_DEV.md.

## Repository Scope

This shared repository is firmware-focused.

Included:
- Embedded sources in src
- CMake build and presets
- Vendor SDK integration under vendor
- Minimal test target under test_minimal

Excluded from this shared branch:
- Android receiver application
- Executive summary material
- Python tooling scripts

## Project Layout

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── README_DEV.md
├── src
│   ├── initiator
│   ├── responder
│   ├── common
│   ├── uart
│   ├── platform
│   ├── board
│   ├── accel
│   └── ble
├── test_minimal
└── vendor
```

## Runtime Architecture

Two firmware roles are built separately:
- Initiator: sends poll, receives response, computes distance, outputs CSV
- Responder: receives poll, schedules delayed response, embeds timestamps

High-level flow:

```text
Initiator --POLL--> Responder --RESPONSE(with timestamps)--> Initiator --distance--> UART CSV
```

## CSV Output Contract

Primary runtime stream from initiator:

```text
ms,sample,dist
1203,57,2.34
```

Notes:
- dist is in meters
- ms is local initiator timestamp
- serial settings must match host side (460800 baud)

## Build Requirements

- CMake 3.20 or newer
- Ninja
- ARM GCC toolchain (arm-none-eabi)
- Nordic programming tools (nrfjprog) for flashing

The required SDK sources are already vendored in this repository.

## Build

From repository root:

```bash
cmake --preset=initiator_debug
cmake --build --preset initiator_debug

cmake --preset=responder_debug
cmake --build --preset responder_debug
```

Generated firmware files:
- build/initiator_debug/uwb_initiator.hex
- build/responder_debug/uwb_responder.hex

## Flash

Example with nrfjprog:

```bash
nrfjprog --snr <INITIATOR_SNR> --program build/initiator_debug/uwb_initiator.hex --sectorerase --verify --reset
nrfjprog --snr <RESPONDER_SNR> --program build/responder_debug/uwb_responder.hex --sectorerase --verify --reset
```

If only one probe is connected, the --snr argument can be omitted.

## Quick Bring-Up Checklist

1. Flash initiator and responder with matching builds.
2. Power both boards and place them within expected range.
3. Connect serial host to initiator board.
4. Confirm CSV lines are continuously emitted.
5. Move boards and verify distance changes.

## Notes for Reviewers

- Main timing constants: src/common/ranging.h
- Runtime profile definitions: src/common/uwb_profiles.c
- Initiator ranging loop: src/initiator/main_initiator.c
- Responder response loop: src/responder/main_responder.c
- UART implementation: src/uart/uart_log.c

## License

See vendor and project files for applicable licensing terms.
