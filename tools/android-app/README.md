# Leaps UWB CSV Logger Android App

This is the restored Android receiver app from the previous project history, adapted for the current fixed-profile firmware.

The app only receives, displays, records, and shares CSV data. It does not send runtime profile, channel, data-rate, acquisition-period, or ranging-mode commands to the firmware.

## Firmware Contract

Expected CSV line:

```text
ms,sample,iax,iay,iaz,rax,ray,raz
```

Example:

```text
1234,42,-12,8,1024,-10,7,1018
```

Fields:
- `ms`: firmware timestamp in milliseconds
- `sample`: firmware sample counter
- `iax,iay,iaz`: initiator accelerometer values
- `rax,ray,raz`: responder accelerometer values

Parser behavior:
- ignores header/comment lines starting with `#`
- ignores incomplete or invalid lines without crashing
- records only valid 8-column accel-exchange samples

## Technical Choices

- minSdk: 26
- Architecture: MVVM + StateFlow
- UI: Jetpack Compose
- Transport: USB OTG serial
- Serial setup: 460800 baud, 8N1, no flow control
- Background acquisition: ForegroundService + partial wake lock
- CSV storage: MediaStore in `Downloads/UWBReceiver`

## Build and Run

1. Open `tools/android-app` in Android Studio.
2. Let Gradle sync and download dependencies.
3. Run the app on a physical Android device supporting USB OTG.
4. Connect the UWB receiver board by USB OTG cable.
5. Grant USB and notification permissions.

## Runtime Flow

1. Press Connect.
2. Verify status moves to Connected and Hz starts updating.
3. Press Start Recording to create a timestamped CSV.
4. Press Stop Recording to close the file.
5. Press Share last CSV to export it.

## Manual Test Checklist

- [ ] USB disconnected: UI shows reconnecting/disconnected status.
- [ ] USB permission denied: no crash, clear status message.
- [ ] Valid stream: initiator/responder accelerations update continuously.
- [ ] Invalid lines injected: ignored, app remains stable.
- [ ] Recording start: file appears in `Downloads/UWBReceiver`.
- [ ] Recording stop: file is closed and shareable.
- [ ] Screen off for several minutes: acquisition continues.
- [ ] USB unplug/replug: service reconnects without app restart.
