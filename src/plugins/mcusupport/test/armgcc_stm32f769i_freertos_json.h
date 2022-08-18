// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

constexpr auto armgcc_stm32f769i_freertos_json = R"({
  "qulVersion": "@CMAKE_PROJECT_VERSION@",
  "compatVersion": "@COMPATIBILITY_VERSION@",
  "platform": {
    "id": "STM32F769I-DISCOVERY-FREERTOS",
    "vendor": "ST",
    "colorDepths": [
      32
    ],
    "pathEntries": [
      {
        "id": "STM32CubeProgrammer_PATH",
        "id": "STM32CubeProgrammer_PATH",
        "label": "STM32CubeProgrammer",
        "type": "path",
        "defaultValue": {
          "windows": "$PROGRAMSANDFILES/STMicroelectronics/STM32Cube/STM32CubeProgrammer/",
          "unix": "$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/"
        },
        "optional": false
      }
    ],
    "environmentEntries": [],
    "cmakeEntries": [
      {
        "id": "Qul_DIR",
        "label": "Qt for MCUs SDK",
        "type": "path",
        "cmakeVar": "Qul_ROOT",
        "optional": false
      }
    ]
  },
  "toolchain": {
    "id": "armgcc",
    "versions": [
      "9.3.1",
      "10.3.1"
    ],
    "compiler": {
        "id": "ARMGCC_DIR",
        "label": "GNU Arm Embedded Toolchain",
        "cmakeVar": "QUL_TARGET_TOOLCHAIN_DIR",
        "envVar": "ARMGCC_DIR",
        "setting": "GNUArmEmbeddedToolchain",
        "type": "path",
        "optional": false,
        "versionDetection" : {
            "filePattern": "bin/arm-none-eabi-g++",
            "executableArgs": "--version",
            "regex": "\\bv(\\d+\\.\\d+\\.\\d+)\\b"
        }
      },
      "file": {
        "id": "ARMGCC_CMAKE_TOOLCHAIN_FILE",
        "label": "CMake Toolchain File",
        "cmakeVar": "CMAKE_TOOLCHAIN_FILE",
        "type": "file",
        "defaultValue": "/opt/qtformcu/2.2//lib/cmake/Qul/toolchain/armgcc.cmake",
        "visible": false,
        "optional": false
      }
  },
  "boardSdk": {
    "envVar": "STM32Cube_FW_F7_SDK_PATH",
    "setting": "STM32Cube_FW_F7_SDK_PATH",
    "versions": [ "1.16.0" ],
    "versionDetection" : {
        "filePattern": "package.xml",
        "xmlElement": "PackDescription",
        "xmlAttribute": "Release",
        "regex": "\\b(\\d+\\.\\d+\\.\\d+)\\b"
    },
    "label": "Board SDK for STM32F769I-Discovery",
    "cmakeVar": "QUL_BOARD_SDK_DIR",
    "type": "path",
    "optional": false
  },
  "freeRTOS": {
    "envVar": "STM32F7_FREERTOS_DIR",
    "cmakeEntries": [
      {
        "id": "ST_FREERTOS_DIR",
        "label": "FreeRTOS SDK for STM32F769I-Discovery",
        "cmakeVar": "FREERTOS_DIR",
        "defaultValue": "$QUL_BOARD_SDK_DIR/Middlewares/Third_Party/FreeRTOS/Source",
        "type": "path",
        "optional": false
      }
    ]
  }
})";
