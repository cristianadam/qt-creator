// Copyright  (C) 2016 AudioCodes Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcsbasesubmiteditor.h>

namespace ClearCase {
namespace Internal {

class ClearCaseSubmitEditorWidget;

class ClearCaseSubmitEditor : public VcsBase::VcsBaseSubmitEditor
{
    Q_OBJECT

public:
    ClearCaseSubmitEditor();

    static QString fileFromStatusLine(const QString &statusLine);

    void setStatusList(const QStringList &statusOutput);
    ClearCaseSubmitEditorWidget *submitEditorWidget();

    void setIsUcm(bool isUcm);

protected:
    QByteArray fileContents() const override;
};

} // namespace Internal
} // namespace ClearCase
