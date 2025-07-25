// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "basetexteditmodifier.h"
#include "qmljseditor/qmljseditor.h"

#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljs/parser/qmljsast_p.h>
#include <qmljstools/qmljsindenter.h>
#include <qmljseditor/qmljseditordocument.h>
#include <qmljseditor/qmljscomponentfromobjectdef.h>
#include <qmljseditor/qmljscompletionassist.h>
#include <qmljstools/qmljstoolssettings.h>
#include <texteditor/tabsettings.h>
#include <utils/changeset.h>

using namespace QmlDesigner;

BaseTextEditModifier::BaseTextEditModifier(TextEditor::TextEditorWidget *textEdit)
    : PlainTextEditModifier(textEdit->document())
    , m_textEdit{textEdit}
{
}

void BaseTextEditModifier::indentLines(int startLine, int endLine)
{
    if (startLine < 0)
        return;

    if (!m_textEdit)
        return;

    QmlJSEditor::indentQmlJs(textDocument(), startLine, endLine,
                             m_textEdit->textDocument()->tabSettings());
}

void BaseTextEditModifier::indent(int offset, int length)
{
    if (length == 0 || offset < 0 || offset + length >= text().length())
        return;

    int startLine = getLineInDocument(textDocument(), offset);
    int endLine = getLineInDocument(textDocument(), offset + length);

    if (startLine > -1 && endLine > -1)
        indentLines(startLine, endLine);
}

TextEditor::TabSettings BaseTextEditModifier::tabSettings() const
{
    if (m_textEdit)
        return m_textEdit->textDocument()->tabSettings();
    return QmlJSTools::globalQmlJSCodeStyle()->tabSettings();
}

bool BaseTextEditModifier::renameId(const QString &oldId, const QString &newId)
{
    if (m_textEdit) {
        if (auto document = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(
                m_textEdit->textDocument())) {
            Utils::ChangeSet changeSet;
            const QList<QmlJS::SourceLocation> locations = document->semanticInfo()
                                                               .idLocations.value(oldId);
            for (const QmlJS::SourceLocation &loc : locations) {
                changeSet.replace(loc.begin(), loc.end(), newId);
            }
            QTextCursor tc = textCursor();
            changeSet.apply(&tc);
            return true;
        }
    }
    return false;
}

static QmlJS::AST::UiObjectDefinition *getObjectDefinition(const QList<QmlJS::AST::Node *> &path, QmlJS::AST::UiQualifiedId *qualifiedId)
{
    QmlJS::AST::UiObjectDefinition *object = nullptr;
    for (int i = path.size() - 1; i >= 0; --i) {
        auto node = path.at(i);
        if (auto objDef =  QmlJS::AST::cast<QmlJS::AST::UiObjectDefinition *>(node)) {
            if (objDef->qualifiedTypeNameId == qualifiedId)
                object = objDef;
        }
    }
    return object;
}

bool BaseTextEditModifier::moveToComponent(int nodeOffset, const QString &importData)
{
    if (m_textEdit) {
        if (auto document = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(
                m_textEdit->textDocument())) {
            auto qualifiedId = QmlJS::AST::cast<QmlJS::AST::UiQualifiedId *>(document->semanticInfo().astNodeAt(nodeOffset));
            QList<QmlJS::AST::Node *> path = document->semanticInfo().rangePath(nodeOffset);
            QmlJS::AST::UiObjectDefinition *object = getObjectDefinition(path, qualifiedId);

            if (!object)
                return false;

            QmlJSEditor::performComponentFromObjectDef(qobject_cast<QmlJSEditor::QmlJSEditorWidget *>(
                                                           m_textEdit),
                                                       document->filePath().toUrlishString(),
                                                       object,
                                                       importData);
            return true;
        }
    }
    return false;
}

QStringList BaseTextEditModifier::autoComplete(QTextDocument *textDocument, int position, bool explicitComplete)
{
    if (m_textEdit)
        if (auto document = qobject_cast<QmlJSEditor::QmlJSEditorDocument *>(
                m_textEdit->textDocument()))
            return QmlJSEditor::qmlJSAutoComplete(textDocument,
                                                  position,
                                                  document->filePath(),
                                                  explicitComplete ? TextEditor::ExplicitlyInvoked : TextEditor::ActivationCharacter,
                                                  document->semanticInfo());
    return {};
}
