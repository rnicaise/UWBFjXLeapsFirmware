# UWB Receiver Android App

Android app to connect to the initiator board over USB OTG serial, parse the fixed SS-TWR CSV stream, display live distance, and record sessions to Downloads/UWBReceiver.

## Project tree

```text
android-receiver-app/
  settings.gradle.kts
  build.gradle.kts
  gradle.properties
  app/
    build.gradle.kts
    proguard-rules.pro
    src/main/
      AndroidManifest.xml
      java/com/qorvo/uwbreceiver/
        UwbReceiverApp.kt
        MainActivity.kt
        data/
          CsvParser.kt
          RecordingManager.kt
          RuntimeStore.kt
          SettingsStore.kt
          UwbModels.kt
        service/
          UwbForegroundService.kt
        ui/
          UwbMainScreen.kt
          theme/
            Color.kt
            Theme.kt
            Type.kt
        viewmodel/
          UwbViewModel.kt
      res/values/
        strings.xml
        themes.xml
```

## Technical choices

- minSdk: 26
- targetSdk / compileSdk: 35
- Architecture: MVVM + StateFlow
- USB serial library: com.github.mik3y:usb-serial-for-android
- Serial setup: 460800, 8N1, no flow control
- UWB mode: fixed SS-TWR, channel 5, 6.8 Mbps
- ForegroundService used for robust background acquisition
- CSV recording through MediaStore in Downloads/UWBReceiver

## CSV contract

Expected firmware line:

```text
ms,sample,dist,iax,iay,iaz,rax,ray,raz
```

Example:

```text
1234,42,1.27,18,-42,1001,12,-39,998
```

Accelerometer fields are signed integer milligravity values:

- `iax,iay,iaz`: initiator X/Y/Z
- `rax,ray,raz`: responder X/Y/Z, transported back over the UWB Response

Parser behavior:

- ignores header and comments starting with #
- ignores incomplete or invalid lines without crashing
- does not send runtime UWB mode, channel, or profile commands

## Build and run

1. Open android-receiver-app in Android Studio (Ladybug+ recommended).
2. Let Gradle sync and download dependencies.
3. Run app on a physical Android device supporting USB OTG.
4. Connect the initiator board by USB OTG cable.
5. Grant USB and notification permissions.

## Runtime flow

1. Press Connect.
2. Verify status moves to Connected and Hz starts updating.
3. Press Start Recording to create a timestamped CSV.
4. Press Stop Recording to close file.
5. Press Share last CSV to export via Android share sheet.

## Manual test checklist

- [ ] USB disconnected: UI shows reconnecting/disconnected status.
- [ ] USB permission denied: no crash, clear status message.
- [ ] Valid stream: distance updates continuously.
- [ ] Invalid lines injected: ignored, app remains stable.
- [ ] Recording start: file appears in Downloads/UWBReceiver.
- [ ] Recording stop: file closed and shareable.
- [ ] Screen off for several minutes: acquisition still running.
- [ ] USB unplug/replug: service reconnects without app restart.
- [ ] Distance color thresholds react to settings sliders.

## Notes

- Foreground notification shows live status, distance, and recording state.
- Partial wake lock is held while the service is active.
- Threshold and experiment-position sliders are persisted with DataStore.
