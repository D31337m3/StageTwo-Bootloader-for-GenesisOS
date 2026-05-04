# GenesisOS StageTwo (Stage2 Boot Manager)

`GenesisOS-StageTwo` is a advanced Boot Manager for Genesis OS on ESP32-S3 devices with built in displays. It runs immediately after the ESP-IDF bootloader and before GenesisOS.

It provides a GenesisOS-styled splash + boot UI, recovery/repair tooling, and update flows, while keeping destructive actions gated behind an **8-second BOOT-button hold** confirmation.

## Boot flow

```text
ROM Bootloader
-> ESP-IDF Bootloader
-> StageTwo factory app
-> GenesisOS OR Recovery/Menu
```

StageTwo is implemented as a **factory app partition** (not inside the raw ESP-IDF bootloader).

## Hardware scope

StageTwo initializes only:

- Display
- Touch
- BOOT button input
- Wi-Fi
- SD card
- NVS
- Flash partitions

## Behavior

On boot:

- No input: boot GenesisOS
- Double tap BOOT: open the `StageTwo` boot menu
- Hold BOOT ~5 seconds: Recovery Mode
- NVS recovery flag: Recovery Mode

## UI

### StageTwo Splash Screen

- Despite being coded into stagetwo, the splash screen is actually displaying the GenisisOS branding to create a seamless transition into the OS. This is intentional to make the boot process feel more integrated and polished, even though it's technically part of the boot manager.
It features a colorful 'GENESISOS' animation with a dynamic gradient and subtle particle effects, designed to evoke a sense of awakening and potential. The animation is optimized for the ESP32-S3's display capabilities, ensuring smooth performance without compromising the boot time. Below it is a 5 second countdown progress bar which shows a visual representation of the time remaining before it automatically boots into GenesisOS, giving users a clear indication of the boot process and allowing them to interrupt it if they wish to access the boot menu or recovery options.

### StageTwo Boot Menu
- Title: `StageTwo` (`Stage` animated pulse, `Two` static purple, taking cues from the splash) 
- Build label: `Build: <STAGETWO_BUILD_ID>`
- Timeout: 10 seconds (boots GenesisOS)

Menu items:

1. Boot GenesisOS
2. Recovery Mode
3. Install Update
4. Check for Updates
5. Advanced

### Advanced menu

Title: `StageTwo Advanced`

1. Format User Partitions
2. Factory Reset
3. Clear NVS
4. Reset
5. Back

### Destructive action confirmation

All destructive actions require an **8-second BOOT-button hold**:

- Progress bar fills 0–100% with an 8 -> 0 countdown
- Release cancels immediately
- No input cancels
- Touch alone must never confirm destructive actions

## Partition layout (high level)

- `stagetwo` (app, 2MB): StageTwo boot manager (factory app)
- `genesis_*` (apps): GenesisOS slots
- `python` (fat): mounted at `/mpython`
- `user` (fat): mounted at `/user`

See `partitions/genesis_stage2_partitions.csv` for exact offsets/sizes.

## Repo layout

- `components/`: hardware + UI + StageTwo subsystems
- `main/`: application entry (`app_main.cpp`)
- `partitions/`: partition table CSV(s)

## Build (WSL preferred)

This project is typically built from WSL using the repo-local ESP-IDF checkout in the parent Genesis OS tree.

From `StageTwo_Dev/`:

```bash
source ../esp-idf/esp-idf/export.sh >/dev/null 2>&1
idf.py set-target esp32s3
idf.py build
```

To flash (optional):

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Hardware reference

This project targets the Waveshare ESP32-S3-Touch-AMOLED-1.8.

- Docs + resources: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8/Resources-And-Documents

## Notes

- StageTwo must never erase its own factory partition and must always keep recovery accessible.
- Local development notes may exist in `DEV_NOTES.md` (often gitignored by design for personal/internal tracking).
