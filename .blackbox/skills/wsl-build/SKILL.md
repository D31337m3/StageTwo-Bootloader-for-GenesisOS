---
name: wsl-build
description: Brief description of what this Skill does and when to use it
---

# Wsl Build

## Instructions
when building stagetwo_dev or genesisos_rtm builds must use the proper commands to ensure the build is successful. Follow the steps below to build the project correctly.
1. Open a terminal and navigate to the project directory.
2. Run the following command to build the project inside WSL:
   
wsl bash -lc "cd ~/genesis_os/esp-idf/esp-idf/ && . ./export.sh && cd ~/genesis_os/StageTwo_Dev && idf.py build"

3. Wait for the build process to complete. This may take some time depending on your system's performance.
4. Once the build is finished, you can find the generated files in the appropriate output directory
5. If you encounter any errors during the build process, review the error messages and determine solutions based on the specific issues encountered. Common issues may include missing dependencies, incorrect environment setup, or syntax errors in the code.
6. After resolving any issues, repeat the build process until it completes successfully.

