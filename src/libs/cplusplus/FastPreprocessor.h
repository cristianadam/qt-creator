// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "PreprocessorClient.h"
#include "CppDocument.h"
#include "pp.h"

#include <cplusplus/Control.h>

#include <QSet>
#include <QString>

namespace CPlusPlus {

class CPLUSPLUS_EXPORT FastPreprocessor: public Client
{
    Environment _env;
    Snapshot _snapshot;
    Preprocessor _preproc;
    QSet<QString> _merged;
    Document::Ptr _currentDoc;
    bool _addIncludesToCurrentDoc;

    void mergeEnvironment(const QString &fileName);

public:
    FastPreprocessor(const Snapshot &snapshot);

    QByteArray run(Document::Ptr newDoc,
                   const QByteArray &source,
                   bool mergeDefinedMacrosOfDocument = false);

    // CPlusPlus::Client
    virtual void sourceNeeded(int line, const QString &fileName, IncludeType mode,
                              const QStringList &initialIncludes = QStringList());

    virtual void macroAdded(const Macro &);

    virtual void passedMacroDefinitionCheck(int, int, int, const Macro &);
    virtual void failedMacroDefinitionCheck(int, int, const ByteArrayRef &);

    virtual void notifyMacroReference(int, int, int, const Macro &);

    virtual void startExpandingMacro(int,
                                     int,
                                     int,
                                     const Macro &,
                                     const QVector<MacroArgumentReference> &);
    virtual void stopExpandingMacro(int, const Macro &) {}
    virtual void markAsIncludeGuard(const QByteArray &macroName);

    virtual void startSkippingBlocks(int) {}
    virtual void stopSkippingBlocks(int) {}
};

} // namespace CPlusPlus
