// Copyright  (C) 2016 AudioCodes Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QHash>
#include <QString>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

namespace ClearCase {
namespace Internal {

enum DiffType
{
    GraphicalDiff,
    ExternalDiff
};

class ClearCaseSettings
{
public:
    ClearCaseSettings();

    void fromSettings(QSettings *);
    void toSettings(QSettings *) const;

    inline int longTimeOutS() const { return timeOutS * 10; }

    bool equals(const ClearCaseSettings &s) const;

    friend bool operator==(const ClearCaseSettings &p1, const ClearCaseSettings &p2)
    { return p1.equals(p2); }
    friend bool operator!=(const ClearCaseSettings &p1, const ClearCaseSettings &p2)
    { return !p1.equals(p2); }

    QString ccCommand;
    Utils::FilePath ccBinaryPath;
    DiffType diffType = GraphicalDiff;
    QString diffArgs;
    QString indexOnlyVOBs;
    QHash<QString, int> totalFiles;
    bool autoAssignActivityName = true;
    bool autoCheckOut = true;
    bool noComment = false;
    bool keepFileUndoCheckout = true;
    bool promptToCheckIn = false;
    bool disableIndexer = false;
    bool extDiffAvailable = false;
    int historyCount;
    int timeOutS;
};

} // namespace Internal
} // namespace ClearCase
