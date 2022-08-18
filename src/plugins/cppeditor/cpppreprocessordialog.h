// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>
#include <QString>

namespace CppEditor {
namespace Internal {
namespace Ui { class CppPreProcessorDialog; }

class CppPreProcessorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CppPreProcessorDialog(const QString &filePath, QWidget *parent);
    ~CppPreProcessorDialog() override;

    int exec() override;

    QString extraPreprocessorDirectives() const;

private:
    Ui::CppPreProcessorDialog *m_ui;
    const QString m_filePath;
    const QString m_projectPartId;
};

} // namespace Internal
} // namespace CppEditor
