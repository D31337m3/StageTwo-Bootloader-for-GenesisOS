# Useful ESP-IDF Commands

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py partition-table
idf.py flash monitor
```

## Factory Flash Bundle (StageTwo + GenesisOS_RTM)

Goal: one "master" factory image that installs:

- `stagetwo` factory app (StageTwo boot manager)
- `genesis_a` (OTA0) GenesisOS_RTM app (primary slot)
- `genesis_b` (OTA1) GenesisOS_RTM app (secondary/OTA slot)
- `genesis_gold` (test) GenesisOS_RTM app (golden recovery image; never overwritten by OTA downloads)

### Build + bundle (WSL)

From the Genesis OS project root:

```bash
source esp-idf/esp-idf/export.sh >/dev/null 2>&1
cd GenesisOS_RTM && idf.py build
cd ../StageTwo_Dev && idf.py build
cd .. && ./bundle_stage2_rtm.sh
```

Output:

- `Release_binaries/StageTwo/GenesisOS_factory.bin`
