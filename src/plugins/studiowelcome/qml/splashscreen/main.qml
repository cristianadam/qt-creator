// Copyright  (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

import QtQuick 2.0

Item {
    id: root
    width: 600
    height: 720

    signal closeClicked
    signal checkBoxToggled
    signal configureClicked

    property alias doNotShowAgain: welcome_splash.doNotShowAgain

    function onPluginInitialized(crashReportingEnabled: bool, crashReportingOn: bool)
    {
        welcome_splash.onPluginInitialized(crashReportingEnabled, crashReportingOn);
    }

    Welcome_splash {
        id: welcome_splash
        x: 0
        y: 0
        antialiasing: true
        onCloseClicked: root.closeClicked()
        onConfigureClicked: root.configureClicked()
    }
}
