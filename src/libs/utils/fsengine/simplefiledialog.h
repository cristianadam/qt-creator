// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "../utils_global.h"
#include "../filepath.h"

#include <QDialog>
#include <QComboBox>

namespace Utils {
    class QTCREATOR_UTILS_EXPORT SimpleFileDialog : public QDialog
    {
    public:
        SimpleFileDialog(QWidget *parent = nullptr);

        FilePath selectedFilePath() const;

    private:
        FilePath m_selectedFilePath;
    };
}