# AGENTS.md — GenesisOS StageTwo Boot Manager

## Mission

Build an ESP32-S3 ESP-IDF project named `GenesisOS-StageTwo`.

This project is a Stage2 Boot Manager Application that runs immediately after the ESP-IDF bootloader and before GenesisOS.

It provides:

- Splash screen
- Boot menu named `StageTwo`
- Recovery console
- OTA update system
- Backup/restore tools
- Destructive-action safety confirmation

## Boot Flow Architecture

```text
ROM Bootloader
→ ESP-IDF Bootloader
→ StageTwo factory app
→ GenesisOS OR Recovery/Menu
```

StageTwo is implemented as a factory app partition, not inside the raw ESP-IDF bootloader.

## Hardware Scope

Initialize only:

- Display
- Touch
- Boot button
- Wi-Fi
- SD card
- NVS
- Flash partitions

## Core Modules

```text
components/
  genesis_display/
  genesis_touch/
  genesis_sdcard/
  genesis_theme/
  stage2_input/
  stage2_ui/
  stage2_boot/
  stage2_recovery/
  stage2_ota/
  stage2_image/
  stage2_nvs/
  stage2_repair/
  stage2_manifest/
  stage2_logging/
main/
  app_main.cpp
partitions/
  genesis_stage2_partitions.csv
```

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

## StageTwo Boot Menu

Top-left title: `StageTwo`

- `Stage` uses animated color shifting from splash.
- `Two` uses static purple matching the splash `OS` word.

Top-right:

```text
Build: <STAGETWO_BUILD_ID>
```

Menu items:

```text
Boot GenesisOS
Recovery Mode
Install Update
Check for Updates
Advanced
```

The menu is centered with even spacing and has a 10-second timeout that boots GenesisOS.

## Advanced Menu

Title: `StageTwo Advanced`

Items:

```text
Format User Partitions
Factory Reset
Clear NVS
Reset
Back
```

## Destructive Action Confirmation

All destructive actions require an 8-second BOOT-button hold confirmation.

- Progress bar fills from 0–100%.
- Countdown shows 8 → 0.
- Release cancels instantly.
- No input cancels.
- Touch alone must never confirm destructive actions.

## Theming

StageTwo must use GenesisOS colors, typography, spacing, splash animations, and visual style. No default LVGL styling should remain in final UI.

## Safety Rules

1. Never flash without checksum validation.
2. Never erase StageTwo/factory partition.
3. Never erase user data without confirmation.
4. Never allow touch-only destructive confirmation.
5. Always keep recovery accessible.
6. Always log destructive actions.
7. Always fail safely.
