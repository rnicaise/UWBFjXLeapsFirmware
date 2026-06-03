# Android App (CSV-Only)

This app scaffold is aligned with the current fixed firmware behavior:

- no runtime firmware profile switching from Android
- only CSV ingestion/logging
- expected payload format: ms,sample,iax,iay,iaz,rax,ray,raz

## Status

This is a minimal starter app with:
- parser for 8-column CSV lines
- local file logger per session
- simple UI with Start/Stop and manual line ingestion

Transport integration (Bluetooth/UART/USB) is intentionally left as a next step.

## Next Integration Step

Connect incoming lines from your transport layer and call:

- `ingestRawLine(rawLine)` in `MainActivity`

## Output Location

CSV files are stored under app external files:
- `Android/data/com.qorvo.leapscsv/files/sessions/`
