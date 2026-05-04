# GenesisOS StageTwo (Stage2 Boot Manager)

StageTwo is an ESP32-S3 ESP-IDF project that runs immediately after the ESP-IDF bootloader and before GenesisOS.

It provides a GenesisOS-styled splash + boot UI, recovery/repair tooling, and update flows, while keeping destructive actions gated behind BOOT-button hold confirmation.

## Boot flow

```text
ROM Bootloader
→ ESP-IDF Bootloader
→ StageTwo factory app
→ GenesisOS OR Recovery/Menu
```

StageTwo is implemented as a **factory app partition** (not inside the raw ESP-IDF bootloader).

## Hardware scope

StageTwo initializes only:

- Display
- Touch
- BOOT button input
- Wi‑Fi
- SD card
- NVS
- Flash partitions

## UI flow

### StageTwo boot menu

- Title: `StageTwo` (`Stage` animated brand pulse, `Two` static purple)
- Build label: `Build: <STAGETWO_BUILD_ID>`
- 10-second timeout boots GenesisOS

Menu items:

1. Boot GenesisOS
2. Recovery Mode
3. Install Saved Update
4. Download Update
5. Advanced
6. About

### About

- Displays build/version metadata and credits
- Includes the dedication marquee message
- Close: hold BOOT

### Advanced menu

Title: `StageTwo Advanced`

1. Format User Partitions
2. Factory Reset
3. Clear NVS
4. Wi‑Fi Setup
5. Backup User -> SD
6. Restore User <- SD
7. Repartition (SD)
8. Reset
9. Back (returns to main boot menu)

### Destructive action confirmation

All destructive actions require an **8-second BOOT-button hold**:

- Progress bar fills 0–100% with 8 → 0 countdown
- Release cancels immediately
- No input cancels
- Touch alone must never confirm destructive actions

## Partition layout (high level)

- `stagetwo` (app, 2MB): StageTwo boot manager (factory app)
- `genesis_a` / `genesis_b` / `genesis_gold` (app, 3MB each): GenesisOS slots
- `python` (fat, 1MB): mounted at `/mpython`
- `user` (fat, ~3.875MB): mounted at `/user`

See `partitions/genesis_stage2_partitions.csv` for exact offsets/sizes.

## Key modules

- `components/stage2_ui/`: LVGL UI screens/menus
- `components/stage2_input/`: BOOT button events
- `components/stage2_boot/`: partition selection + boot
- `components/stage2_ota/`: update flows, manifest handling
- `components/stage2_repair/`: format/reset/NVS tools
- `components/genesis_display/`, `components/genesis_touch/`, `components/genesis_sdcard/`, `components/genesis_theme/`: shared GenesisOS hardware/UI foundations

## Build (WSL preferred)

From `StageTwo_Dev/`:

```bash
source ../esp-idf/esp-idf/export.sh >/dev/null 2>&1
idf.py set-target esp32s3
idf.py build
```

## Notes

- Project state and decisions live in `DEV_NOTES.md` (treat it as the project ledger).
- StageTwo should never erase its own factory partition and must always keep recovery accessible.
