// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangformatsettings.h"

#include "../beautifierconstants.h"
#include "../beautifiertr.h"

#include <QDateTime>
#include <QXmlStreamWriter>

#include <coreplugin/icore.h>

namespace Beautifier::Internal {

const char SETTINGS_NAME[]               = "clangformat";

ClangFormatSettings::ClangFormatSettings() :
    AbstractSettings(SETTINGS_NAME, ".clang-format")
{
    setCommand("clang-format");

    registerAspect(&usePredefinedStyle);
    usePredefinedStyle.setSettingsKey("usePredefinedStyle");
    usePredefinedStyle.setDefaultValue(true);

    registerAspect(&predefinedStyle);
    predefinedStyle.setSettingsKey("predefinedStyle");
    predefinedStyle.setDefaultValue("LLVM");

    registerAspect(&fallbackStyle);
    fallbackStyle.setSettingsKey("fallbackStyle");
    fallbackStyle.setDefaultValue("Default");

    registerAspect(&customStyle);
    customStyle.setSettingsKey("customStyle");

    read();
}

QString ClangFormatSettings::documentationFilePath() const
{
    return (Core::ICore::userResourcePath() / Beautifier::Constants::SETTINGS_DIRNAME
                / Beautifier::Constants::DOCUMENTATION_DIRNAME / SETTINGS_NAME)
            .stringAppended(".xml")
        .toString();
}

void ClangFormatSettings::createDocumentationFile() const
{
    QFile file(documentationFilePath());
    const QFileInfo fi(file);
    if (!fi.exists())
        fi.dir().mkpath(fi.absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;

    QXmlStreamWriter stream(&file);
    stream.setAutoFormatting(true);
    stream.writeStartDocument("1.0", true);
    stream.writeComment("Created " + QDateTime::currentDateTime().toString(Qt::ISODate));
    stream.writeStartElement(Constants::DOCUMENTATION_XMLROOT);

    const QStringList lines = {
        "BasedOnStyle {string: LLVM, Google, Chromium, Mozilla, WebKit}",
        "AccessModifierOffset {int}",
        "AlignEscapedNewlinesLeft {bool}",
        "AlignTrailingComments {bool}",
        "AllowAllParametersOfDeclarationOnNextLine {bool}",
        "AllowShortFunctionsOnASingleLine {bool}",
        "AllowShortIfStatementsOnASingleLine {bool}",
        "AllowShortLoopsOnASingleLine {bool}",
        "AlwaysBreakBeforeMultilineStrings {bool}",
        "AlwaysBreakTemplateDeclarations {bool}",
        "BinPackParameters {bool}",
        "BreakBeforeBinaryOperators {bool}",
        "BreakBeforeBraces {BraceBreakingStyle: BS_Attach, BS_Linux, BS_Stroustrup, BS_Allman, BS_GNU}",
        "BreakBeforeTernaryOperators {bool}",
        "BreakConstructorInitializersBeforeComma {bool}",
        "ColumnLimit {unsigned}",
        "CommentPragmas {string}",
        "ConstructorInitializerAllOnOneLineOrOnePerLine {bool}",
        "ConstructorInitializerIndentWidth {unsigned}",
        "ContinuationIndentWidth {unsigned}",
        "Cpp11BracedListStyle {bool}",
        "IndentCaseLabels {bool}",
        "IndentFunctionDeclarationAfterType {bool}",
        "IndentWidth {unsigned}",
        "Language {LanguageKind: LK_None, LK_Cpp, LK_JavaScript, LK_Proto}",
        "MaxEmptyLinesToKeep {unsigned}",
        "NamespaceIndentation {NamespaceIndentationKind: NI_None, NI_Inner, NI_All}",
        "ObjCSpaceAfterProperty {bool}",
        "ObjCSpaceBeforeProtocolList {bool}",
        "PenaltyBreakBeforeFirstCallParameter {unsigned}",
        "PenaltyBreakComment {unsigned}",
        "PenaltyBreakFirstLessLess {unsigned}",
        "PenaltyBreakString {unsigned}",
        "PenaltyExcessCharacter {unsigned}",
        "PenaltyReturnTypeOnItsOwnLine {unsigned}",
        "PointerBindsToType {bool}",
        "SpaceBeforeAssignmentOperators {bool}",
        "SpaceBeforeParens {SpaceBeforeParensOptions: SBPO_Never, SBPO_ControlStatements, SBPO_Always}",
        "SpaceInEmptyParentheses {bool}",
        "SpacesBeforeTrailingComments {unsigned}",
        "SpacesInAngles {bool}",
        "SpacesInCStyleCastParentheses {bool}",
        "SpacesInContainerLiterals {bool}",
        "SpacesInParentheses {bool}",
        "Standard {LanguageStandard: LS_Cpp03, LS_Cpp11, LS_Auto}",
        "TabWidth {unsigned}",
        "UseTab {UseTabStyle: UT_Never, UT_ForIndentation, UT_Always}"
    };

    for (const QString& line : lines) {
        const int firstSpace = line.indexOf(' ');
        const QString keyword = line.left(firstSpace);
        const QString options = line.right(line.size() - firstSpace).trimmed();
        const QString text = "<p><span class=\"option\">" + keyword
                + "</span> <span class=\"param\">" + options
                + "</span></p><p>" + Tr::tr("No description available.") + "</p>";
        stream.writeStartElement(Constants::DOCUMENTATION_XMLENTRY);
        stream.writeTextElement(Constants::DOCUMENTATION_XMLKEY, keyword);
        stream.writeTextElement(Constants::DOCUMENTATION_XMLDOC, text);
        stream.writeEndElement();
    }

    stream.writeEndElement();
    stream.writeEndDocument();
}

QStringList ClangFormatSettings::completerWords()
{
    return {
        "LLVM",
        "Google",
        "Chromium",
        "Mozilla",
        "WebKit",
        "BS_Attach",
        "BS_Linux",
        "BS_Stroustrup",
        "BS_Allman",
        "NI_None",
        "NI_Inner",
        "NI_All",
        "LS_Cpp03",
        "LS_Cpp11",
        "LS_Auto",
        "UT_Never",
        "UT_ForIndentation",
        "UT_Always"
    };
}

//void ClangFormatSettings::setPredefinedStyle(const QString &predefinedStyle)
//{
//    const QStringList test = predefinedStyles();
//    if (test.contains(predefinedStyle))
//        m_settings.insert(PREDEFINED_STYLE, QVariant(predefinedStyle));
//}

//void ClangFormatSettings::setFallbackStyle(const QString &fallbackStyle)
//{
//    const QStringList test = fallbackStyles();
//    if (test.contains(fallbackStyle))
//        m_settings.insert(FALLBACK_STYLE, QVariant(fallbackStyle));
//}

QStringList ClangFormatSettings::predefinedStyles() const
{
    return {"LLVM", "Google", "Chromium", "Mozilla", "WebKit", "File"};
}

QStringList ClangFormatSettings::fallbackStyles() const
{
    return {"Default", "None", "LLVM", "Google", "Chromium", "Mozilla", "WebKit"};
}

QString ClangFormatSettings::styleFileName(const QString &key) const
{
    return m_styleDir.absolutePath() + '/' + key + '/' + m_ending;
}

void ClangFormatSettings::readStyles()
{
    const QStringList dirs = m_styleDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    for (const QString &dir : dirs) {
        QFile file(m_styleDir.absoluteFilePath(dir + '/' + m_ending));
        if (file.open(QIODevice::ReadOnly))
            m_styles.insert(dir, QString::fromLocal8Bit(file.readAll()));
    }
}

} // Beautifier::Internal
