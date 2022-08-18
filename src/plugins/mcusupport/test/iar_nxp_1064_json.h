// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

constexpr auto iar_nxp_1064_json = R"({
    "compatVersion": "1",
    "qulVersion": "2.0.0",
    "boardSdk": {
         "cmakeVar": "QUL_BOARD_SDK_DIR",
         "label": "Board SDK for MIMXRT1064-EVK",
         "id": "NXP_SDK_DIR",
         "optional": false,
         "type": "path",
         "versions": ["2.11.0"],
         "versionDetection" : {
             "filePattern": "*_manifest_*.xml",
             "xmlElement": "ksdk",
             "xmlAttribute": "version",
             "regex": ".*"
         },
         "envVar": "EVK_MIMXRT1064_SDK_PATH",
         "setting": "EVK_MIMXRT1064_SDK_PATH"
    },
    "freeRTOS": {
        "cmakeEntries": [
            {
                "envVar": "IMXRT1064_FREERTOS_DIR",
                "cmakeVar": "FREERTOS_DIR",
                "defaultValue": "$QUL_BOARD_SDK_DIR/rtos/freertos/freertos_kernel",
                "label": "FreeRTOS Sources (IMXRT1064) ",
                "label": "FreeRTOS SDK for MIMXRT1064-EVK",
                "id": "NXP_FREERTOS_DIR",
                "optional": false,
                "type": "path"
           }
        ],
        "envVar": "IMXRT1064_FREERTOS_DIR"
    },
    "platform": {
        "cmakeEntries": [
            {
                "cmakeVar": "Qul_ROOT",
                "label": "Qt for MCUs SDK",
                "id": "Qul_DIR",
                "optional": false,
                "type": "path"
            },
            {
                "cmakeVar": "MCUXPRESSO_IDE_PATH",
                "defaultValue": {
                    "unix": "/usr/local/mcuxpressoide/",
                    "windows": "$ROOT/nxp/MCUXpressoIDE*"
                }
            }
        ],
        "colorDepths": [16],
        "environmentEntries": [],
        "id": "MIMXRT1064-EVK-FREERTOS",
        "pathEntries": [],
        "vendor": "NXP"
    },
    "toolchain": {
        "id": "iar",
        "versions": ["8.50.9"],
        "compiler": {
                "id": "IAR_DIR",
                "cmakeVar": "QUL_TARGET_TOOLCHAIN_DIR",
                "setting": "IARToolchain",
                "envVar": "IAR_ARM_COMPILER_DIR",
                "label": "IAR ARM Compiler",
                "optional": false,
                "type": "path"
        },
        "file": {
            "id": "IAR_CMAKE_TOOLCHAIN_FILE",
            "label": "CMake Toolchain File",
            "cmakeVar": "CMAKE_TOOLCHAIN_FILE",
            "type": "file",
            "defaultValue": "/opt/qtformcu/2.2//lib/cmake/Qul/toolchain/iar.cmake",
            "visible": false,
            "optional": false
        }
    }
})";
