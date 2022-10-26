// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

constexpr auto ghs_tviic2d6m_baremetal_json = R"(
{
    "qulVersion": "2.3.0",
    "compatVersion": "1",
    "platform": {
        "id": "TVIIC2D6M-BAREMETAL",
        "vendor": "CYPRESS",
        "colorDepths": [
            32
        ],
        "cmakeEntries": [
            {
                "id": "INFINEON_AUTO_FLASH_UTILITY_DIR",
                "setting": "CypressAutoFlashUtil",
                "label": "Cypress Auto Flash Utility",
                "type": "path",
                "cmakeVar": "INFINEON_AUTO_FLASH_UTILITY_DIR",
                "optional": false,
                "addToSystemPath": true
            }
        ]
    },
    "toolchain": {
        "id": "arm-greenhills",
        "versions": [
            "201954"
        ],
        "compiler": {
            "id": "GHS_ARM_DIR",
            "label": "Green Hills Compiler for ARM",
            "setting": "GHSArmToolchain",
            "cmakeVar": "QUL_TARGET_TOOLCHAIN_DIR",
            "type": "path",
            "optional": false
        },
        "file": {
            "id": "GHS_ARM_CMAKE_TOOLCHAIN_FILE",
            "cmakeVar": "CMAKE_TOOLCHAIN_FILE",
            "type": "file",
            "defaultValue": "%{Qul_ROOT}/lib/cmake/Qul/toolchain/ghs-arm.cmake",
            "visible": false,
            "optional": false
        }
    },
    "boardSdk": {
        "envVar": "TVII_GRAPHICS_DRIVER_DIR",
        "setting": "TVII_GRAPHICS_DRIVER_DIR",
        "versions": [
            "V1e.1.0"
        ],
        "id": "TVII_GRAPHICS_DRIVER_DIR",
        "label": "Graphics Driver for Traveo II Cluster Series",
        "cmakeVar": "QUL_BOARD_SDK_DIR",
        "type": "path",
        "optional": false
    }
}
)";
