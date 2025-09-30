// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmldesignercorelib_global.h"

#include <nanotrace/nanotracehr.h>
#include <qmljs/qmljsdocument.h>
#include <texteditor/tabsettings.h>

#include <QObject>
#include <QTextCursor>
#include <QTextDocument>

#include <optional>

namespace TextEditor { class TabSettings; }

namespace QmlDesigner {

class QMLDESIGNERCORE_EXPORT TextModifier: public QObject
{
    Q_OBJECT

private:
    TextModifier(const TextModifier &) = delete;
    TextModifier &operator=(const TextModifier &) = delete;

public:
    struct MoveInfo {
        int objectStart;
        int objectEnd;
        int leadingCharsToRemove;
        int trailingCharsToRemove;

        int destination;
        QString prefixToInsert;
        QString suffixToInsert;

        template<typename String>
        friend void convertToString(String &string, const MoveInfo &property)
        {
            using NanotraceHR::dictionary;
            using NanotraceHR::keyValue;
            auto dict = dictionary(keyValue("object start", property.objectStart),
                                   keyValue("object end", property.objectEnd),
                                   keyValue("leading chars to remove", property.leadingCharsToRemove),
                                   keyValue("trailing chars to remove", property.trailingCharsToRemove),
                                   keyValue("destination", property.destination),
                                   keyValue("prefix to insert", property.prefixToInsert),
                                   keyValue("suffix to insert", property.suffixToInsert));

            convertToString(string, dict);
        }

        MoveInfo()
            : objectStart(-1)
            , objectEnd(-1)
            , leadingCharsToRemove(0)
            , trailingCharsToRemove(0)
            , destination(-1)
        {}
    };

public:
    TextModifier();
    ~TextModifier();

    virtual void replace(int offset, int length, const QString& replacement) = 0;
    virtual void move(const MoveInfo &moveInfo) = 0;
    virtual void indent(int offset, int length) = 0;
    virtual void indentLines(int startLine, int endLine) = 0;

    virtual TextEditor::TabSettings tabSettings() const = 0;

    virtual void startGroup() = 0;
    virtual void flushGroup() = 0;
    virtual void commitGroup() = 0;

    virtual QTextDocument *textDocument() const = 0;
    virtual QString text() const = 0;
    virtual QTextCursor textCursor() const = 0;
    static int getLineInDocument(QTextDocument* document, int offset);

    virtual void deactivateChangeSignals() = 0;
    virtual void reactivateChangeSignals() = 0;

    static QmlJS::Snapshot qmljsSnapshot();

    virtual bool renameId(const QString &oldId, const QString &newId) = 0;
    virtual QStringList autoComplete(QTextDocument * /*textDocument*/, int /*position*/, bool explicitComplete = true) = 0;

    virtual QString moveToComponent(int nodeOffset, const QString &importData) = 0;

    virtual void convertPosition(int /* pos */, int * /* line */, int * /* column */) const {};

signals:
    void textChanged();

    void replaced(int offset, int oldLength, int newLength);
    void moved(const TextModifier::MoveInfo &moveInfo);
};

}
