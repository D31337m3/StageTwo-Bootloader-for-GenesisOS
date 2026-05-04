# LegacyCode (import-only)

This folder holds **GenesisOS legacy code** that must be re-integrated into the
StageTwo boot manager by a separate agent.

Rules:
- **Do not** compile this code as part of StageTwo yet.
- **Do not** “merge” logic here; keep these files as historical source material.

Current contents:
- `genesisos_root/`: code copied out of the GenesisOS root project (boot menu, recovery, splash).

