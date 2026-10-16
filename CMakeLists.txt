cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Do not label a new BLE image with the rollback tag at HEAD.
set(PROJECT_VER "ble-mvp-0.1.3-dev")
project(FoloToy-AI-Passport)
