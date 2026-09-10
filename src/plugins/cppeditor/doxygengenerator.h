// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/commentssettings.h>

#include <QString>
#include <QStringList>

QT_FORWARD_DECLARE_CLASS(QTextCursor)

namespace CPlusPlus { class Snapshot; }
namespace Utils { class FilePath; }

namespace CppEditor::Internal {

class DoxygenGenerator
{
public:
    DoxygenGenerator();

    enum DocumentationStyle {
        JavaStyle,  ///< JavaStyle comment: /**
        QtStyle,    ///< QtStyle comment: /*!
        CppStyleA,  ///< CppStyle comment variant A: ///
        CppStyleB   ///< CppStyle comment variant B: //!
    };

    void setStyle(DocumentationStyle style) { m_style = style; }
    void setSettings(const TextEditor::CommentsSettings::Data &settings) { m_settings = settings; }

    QString generate(QTextCursor cursor,
                     const CPlusPlus::Snapshot &snapshot,
                     const Utils::FilePath &documentFilePath);

    // What a comment says about the declaration under it: the name to write
    // in the brief, the word that goes in front of that name where it is a
    // type's, the parameters to list, and whether anything is returned.
    // Which is the whole of what a syntax tree is asked for here, and none of
    // it depends on which front end read the declaration.
    class DeclarationFacts
    {
    public:
        enum Kind {
            Unnamed,    // nothing to say about it: only the comment itself
            Declarator, // a function or a variable, written under a name
            Aggregate   // a class, struct, union or enum, written under a name
        };

        Kind kind = Unnamed;
        QString name;
        QString aggregate; // "class", "struct", "union" or "enum"
        QStringList parameters;
        bool returnsSomething = false;
    };

private:
    QString write(QTextCursor cursor, const DeclarationFacts &facts);
    QChar styleMark() const;

    enum Command {
        BriefCommand,
        ParamCommand,
        ReturnCommand
    };
    static QString commandSpelling(Command command);

    void writeEnd(QString *comment) const;
    void writeContinuation(QString *comment) const;
    void writeNewLine(QString *comment) const;
    void writeCommand(QString *comment,
                      Command command,
                      const QString &commandContent = QString()) const;
    void writeBrief(QString *comment,
                    const QString &brief,
                    const QString &prefix = QString(),
                    const QString &suffix = QString());

    void assignCommentOffset(QTextCursor cursor);
    QString offsetString() const;

    TextEditor::CommentsSettings::Data m_settings;
    QString m_commentOffset;
    DocumentationStyle m_style = QtStyle;
};

} // namespace CppEditor::Internal
