# GenesisOS StageTwo

ESP32-S3 ESP-IDF StageTwo boot manager scaffold for GenesisOS.

## What this scaffold contains

- ESP-IDF project layout
- Custom partition table
- StageTwo boot flow
- LVGL-ready UI abstraction
- Boot menu and advanced menu stubs
- Recovery mode stubs
- OTA downloader stubs
- Backup/restore stubs
- Destructive action hold-confirm API
- Placeholder GenesisOS driver components

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Integration steps

1. Replace placeholder display driver in `components/genesis_display`.
2. Replace placeholder touch driver in `components/genesis_touch`.
3. Replace placeholder SD mount code in `components/genesis_sdcard`.
4. Copy real splash assets into `components/stage2_ui`.
5. Replace OTA manifest URL in `components/stage2_ota/stage2_ota.cpp`.
6. Adjust partition sizes for your exact flash map.

## Important architecture note

This project is a factory app that behaves like a graphical boot manager. LVGL, Wi-Fi, SD, and recovery logic should not be placed inside the raw ESP-IDF bootloader binary.
