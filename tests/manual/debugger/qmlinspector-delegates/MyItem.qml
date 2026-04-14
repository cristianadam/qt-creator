// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick

Rectangle {
    width: 60
    height: 60
    color: "steelblue"
    border.color: "white"
    border.width: 1

    property string label: ""

    Text {
        anchors.centerIn: parent
        text: parent.label
        color: "white"
        font.pixelSize: 14
    }
}
