// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

constexpr auto armgcc_stm32h750b_metal_json = R"({
  "qulVersion": "2.3.0",
  "compatVersion": "1",
  "platform": {
    "id": "STM32H750B-DISCOVERY-BAREMETAL",
    "vendor": "ST",
    "colorDepths": [
      32
    ],
    "pathEntries": [
      {
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
      "9.3.1"
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
      "file" : {
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
    "envVar": "STM32Cube_FW_H7_SDK_PATH",
    "setting": "STM32Cube_FW_H7_SDK_PATH",
    "versions": [
      "1.5.0"
    ],
    "versionDetection" : {
        "filePattern": "package.xml",
        "xmlElement": "PackDescription",
        "xmlAttribute": "Release",
        "regex": "\\b(\\d+\\.\\d+\\.\\d+)\\b"
    },
    "id": "ST_SDK_DIR",
    "label": "Board SDK for STM32H750B-Discovery",
    "cmakeVar": "QUL_BOARD_SDK_DIR",
    "type": "path",
    "optional": false
  }
})";
