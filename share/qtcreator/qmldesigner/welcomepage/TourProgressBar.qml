/****************************************************************************
**
** Copyright (C) 2023 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

import QtQuick
import QtQuick.Controls
import WelcomeScreen 1.0

Item {
    id: progressBar

    property int endSlide: 10
    property int currentSlide: 1

    Rectangle {
        id: progressBarGroove
        color: "#272727"
        radius: 5
        border.color: "#00000000"
        anchors.fill: parent
    }

    Rectangle {
        id: progressBarTrack
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        anchors.topMargin: 1
        width: (progressBarGroove.width / 100) * rangeMapper.output
        color: "#57b9fc"
        radius: 4.5
    }

    RangeMapper {
        id: rangeMapper
        inputMaximum: progressBar.endSlide
        outputMaximum: 100
        outputMinimum: 0
        inputMinimum: 0
        input: progressBar.currentSlide
    }
}
