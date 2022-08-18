// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0  WITH Qt-GPL-exception-1.0

import QtQuick 2.15
import QtQuickDesignerTheme 1.0
import HelperWidgets 2.0

PropertyEditorPane {
    id: itemPane

    signal toolBarAction(int action)

    // invoked from C++ to refresh material preview image
    function refreshPreview()
    {
        topSection.refreshPreview()
    }

    MaterialEditorTopSection {
        id: topSection

        onToolBarAction: (action) => itemPane.toolBarAction(action)
    }

    Item { width: 1; height: 10 }

    Loader {
        id: specificsOne
        anchors.left: parent.left
        anchors.right: parent.right
        source: specificsUrl
    }
}
