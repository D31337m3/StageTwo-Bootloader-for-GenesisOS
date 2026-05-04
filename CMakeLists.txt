cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# StageTwo app version (shown in esp_app_get_description()->version).
set(PROJECT_VER "0.1.2")
project(GenesisOS-StageTwo)
