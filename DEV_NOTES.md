# GenesisOS StageTwo - Development Notes

## Overview
While technically a third stage in the boot process (the ROM bootloader is the first stage, ESP-IDF bootloader is the second), StageTwo is the Boot Manager Application for GenesisOS. It runs immediately after the ESP-IDF bootloader and before GenesisOS, providing:

- Splash screen 'GenesisOS' animation 
- Boot menu named `StageTwo` with options to boot GenesisOS, enter recovery, manage updates, and advanced tools
- Recovery console
- OTA update system
- Backup/restore tools
- Destructive-action safety confirmation

## Boot Flow Architecture

```
ROM Bootloader
→ ESP-IDF Bootloader
→ StageTwo factory app
→ GenesisOS OR Recovery/Menu
```

StageTwo is implemented as a factory app partition, not inside the raw ESP-IDF bootloader.

## Hardware Scope

Initializes only:
- Display
- Touch
- Boot button
- Wi-Fi
- SD card
- NVS
- Flash partitions

## Boot Behavior

On boot:
1. Init NVS
2. Init display + touch
3. Init input system
4. Show splash
5. Detect input:
   - None: boot GenesisOS
   - Double tap BOOT: open Boot Menu
   - Hold BOOT 5s: Recovery Mode
   - NVS recovery flag: Recovery Mode

## Partition Table

| Name | Type | Size | Purpose |
|------   |-----|-----|---------|
| nvs     | data| 24KB| NVS storage for runtime config 
| phy_init| data| 4KB | PHY initialization data
| stagetwo| app | 2MB | StageTwo Bootloader
| genesis_a | app | 3MB | GenesisOS slot A (OTA0)
| genesis_b | app | 3MB | GenesisOS slot B (OTA1)
| genesis_gold | app | 3MB | Golden recovery slot (TEST)
| python  | fat | 1MB | MicroPython drive (`/mpython`)
| user    | fat | ~3.875MB | User storage (`/user`) incl settings + `wifi.toml`


### Partition Details

- **stagetwo (0x20000, 2MB)**: StageTwo application - the boot manager itself - primary boot target - in turn points to genesis if not alt path triggered.
- **genesis_a (0x220000, 3MB)**: GenesisOS slot A (OTA0). Default boot target when no override is set.
- **genesis_b (0x520000, 3MB)**: GenesisOS slot B (OTA1). Used for updates / fallback.
- **genesis_gold (0x820000, 3MB)**: Golden recovery image used by StageTwo "Repair System".
- **python (0xB20000, 1MB)**: FAT partition mounted at `/mpython`.
- **user (0xC20000, 0x3E0000)**: FAT partition mounted at `/user` for persistent settings + downloads.

## Bundling GenesisOS_RTM

StageTwo is the factory app and boots GenesisOS by selecting an app partition label (`genesis_a` / `genesis_b` / `genesis_gold`).
When bundling a full firmware image:

- Flash StageTwo artifacts (bootloader, partition table, otadata, StageTwo app) using StageTwo build offsets.
- Flash `GenesisOS_RTM/build/genesisos_rtm.bin` into all 3 Genesis slots (`genesis_a`, `genesis_b`, `genesis_gold`) for factory images.

A helper bundling script exists at repo root: `bundle_stage2_rtm.sh` (WSL).

## Core Modules

```
components/
  genesis_display/    - LVGL display integration
  genesis_touch/     - Touch input driver (I2C FT6336U)
  genesis_sdcard/    - SD card support
  genesis_theme/     - GenesisOS color palette
  stage2_input/     - BOOT button handling (GPIO0)
  stage2_ui/         - LVGL-based UI screens
  stage2_boot/       - Partition booting logic
  stage2_recovery/   - Recovery console
  stage2_ota/       - OTA update system
  stage2_image/     - Image validation
  stage2_nvs/       - NVS management
  stage2_repair/    - System repair tools
  stage2_manifest/  - Update manifest parsing
  stage2_logging/   - Logging system
main/
  app_main.cpp      - Application entry point
partitions/
  genesis_stage2_partitions.csv
```

## StageTwo Boot Menu

**Title**: `StageTwo`
- "Stage" uses animated color shifting from splash
- "Two" uses static purple matching the splash

**Top-right**: `Build: <STAGETWO_BUILD_ID>`

**Menu items**:
1. Boot GenesisOS
2. Recovery Mode
3. Install Saved Update
4. Download Update
5. Advanced
6. About

The menu is centered with even spacing and has a 10-second timeout that boots GenesisOS.

## Advanced Menu

**Title**: `StageTwo Advanced`

**Items**:
1. Format User Partitions
2. Factory Reset
3. Clear NVS
4. Wi-Fi Setup
5. Backup User -> SD
6. Restore User <- SD
7. Repartition (SD)
8. Reset
9. Back

## Destructive Action Confirmation

All destructive actions require an 8-second BOOT-button hold confirmation:
- Progress bar fills from 0–100%
- Countdown shows 8 → 0
- Release cancels instantly
- No input cancels
- Touch alone must never confirm destructive actions

## Theming

StageTwo uses GenesisOS colors:
- Primary: `#42D9FF` (cyan)
- Accent: `#FF4DD8` (pink)
- Purple (OS): `#9B5CFF`
- Background: `#050512`
- Text: `#F4F7FF`
- Success: `#20D676`
- Warning: `#FFCC33`
- Error: `#FF3B3B`

## Safety Rules

1. Never flash without checksum validation
2. Never erase StageTwo/factory partition
3. Never erase user data without confirmation
4. Never allow touch-only destructive confirmation

## Change Log

### 2026-05-04

- About dialog: restored dedication marquee message and ensured it renders reliably inside the modal panel.
- Advanced menu: `Back` now exits Advanced and returns to the main StageTwo boot menu (no recursive menu loop).
5. Always keep recovery accessible
6. Always log destructive actions
7. Always fail safely

## Build Configuration

- Target: ESP32-S3
- Flash size: 16MB
- Partition table: Custom (genesis_stage2_partitions.csv)
- LVGL: Enabled for UI

## Build ID

Default build ID: `dev-000003`

Can be overridden at compile time:
```c
#define STAGETWO_BUILD_ID "custom-build-id"
```

## OTA Update System

The OTA system supports:
- Wi-Fi connectivity for update downloads
- Manifest parsing from JSON endpoint
- SHA-256 checksum validation
- Downloads updates into the inactive A/B slot (`genesis_a` / `genesis_b`)
- Save updates to the inactive A/B slot (`genesis_a` / `genesis_b`) and persist the saved slot in NVS
- "Install Saved Update" boots the saved slot

### Update Manifest Format

```json
{
  "version": "1.0.0",
  "url": "https://example.com/genesisos.bin",
  "sha256": "abc123...",
  "size": 1048576
}
```

## Known Limitations

- OTA downloads currently store into the inactive A/B slot (`genesis_a` / `genesis_b`), but there is no UI yet showing which slot is active/inactive.
- Touch coordinate transform may still need tuning (mirror/swap) depending on the current display rotation mode.

## Future Work

- [ ] Add "slot manager" UI (show versions; select restore targets)
- [ ] Complete recovery console features

## 2026-05-02 Changes

- Implemented real Waveshare-style display init (SH8601 QSPI + TCA9554 backlight) and LVGL display registration (`components/genesis_display/genesis_display.cpp`).
- Implemented real touch init using `esp_lcd_touch_ft5x06` with `esp_lcd_new_panel_io_i2c()` IO handle + LVGL indev registration (`components/genesis_touch/genesis_touch.cpp`).
- Added GenesisOS-compatible persistent Wi-Fi creds + UI (`/user/wifi.toml`) and made OTA use autoconnect + "Wi-Fi Setup" path (`components/stage2_wifi/*`, `components/stage2_ui/stage2_ui.cpp`, `components/stage2_ota/stage2_ota.cpp`).
- Implemented real SD card mount (SDSPI on SPI3, CS via TCA9554 EXIO7) at `/sd` (`components/genesis_sdcard/genesis_sdcard.cpp`).
- Added `/user` backup/restore to SD archive (`/sd/stage2/backups/user_backup.gosar`) (`components/stage2_backup/*`, wired into Advanced menu).
- Added repartition-from-SD scaffolding for growing Genesis slot sizes in the future (writes new `partition-table.bin` + reflashes `genesisos.bin` into A/B/Gold, erases `user`/`python`) (`components/stage2_repartition/*`, wired into Advanced menu).
- Added main-menu `About` modal showing build info + credits + dedication marquee (`components/stage2_ui/stage2_ui.cpp`).

## 2026-05-01 Changes

- Switched StageTwo partition table to 3-slot GenesisOS layout: `genesis_a` (OTA0), `genesis_b` (OTA1), and `genesis_gold` (golden recovery) (`partitions/genesis_stage2_partitions.csv`).
- Recovery "Repair System" now restores from `genesis_gold` -> inactive A/B slot and sets next boot to the restored slot (`components/stage2_repair/stage2_repair.cpp`).
- OTA downloads now write into the inactive A/B slot and persist `saved_slot` in NVS; "Install Saved Update" boots that saved slot (`components/stage2_ota/stage2_ota.cpp`).
- Updated root bundler to produce a factory image that flashes GenesisOS_RTM into `genesis_a`, `genesis_b`, and `genesis_gold` (`bundle_stage2_rtm.sh`).

## Build Commands

```bash
# Build in WSL
export IDF_PATH=/home/d31337m3/genesis_os/esp-idf/esp-idf
source $IDF_PATH/export.sh
cd /home/d31337m3/genesis_os/StageTwo_Dev
idf.py build
```

## Version History

- **v0.1.0** (dev-000001): Initial implementation with LVGL UI, boot menu, OTA system
- **v0.1.1** (dev-000002): About marquee restored; Advanced `Back` returns to main menu
- **v0.1.2** (dev-000003): Build/version bump
