// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "uncrustifysettings.h"

#include "../beautifierconstants.h"

#include <coreplugin/icore.h>
#include <utils/process.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamWriter>

using namespace Utils;

namespace Beautifier::Internal {

const char SETTINGS_NAME[]                 = "uncrustify";

UncrustifySettings::UncrustifySettings() :
    AbstractSettings(SETTINGS_NAME, ".cfg")
{
    setVersionRegExp(QRegularExpression("([0-9]{1})\\.([0-9]{2})"));
    setCommand("uncrustify");

    setDocumentationFilePath(Core::ICore::userResourcePath(Beautifier::Constants::SETTINGS_DIRNAME)
        .pathAppended(Beautifier::Constants::DOCUMENTATION_DIRNAME)
        .pathAppended(SETTINGS_NAME)
        .stringAppended(".xml"));

    registerAspect(&useOtherFiles);
    useOtherFiles.setSettingsKey("useOtherFiles");
    useOtherFiles.setDefaultValue(true);

    registerAspect(&useHomeFile);
    useHomeFile.setSettingsKey("useHomeFile");

    registerAspect(&useCustomStyle);
    useCustomStyle.setSettingsKey("useCustomStyle");

    registerAspect(&customStyle);
    customStyle.setSettingsKey("customStyle");

    registerAspect(&formatEntireFileFallback);
    formatEntireFileFallback.setSettingsKey("formatEntireFileFallback");
    formatEntireFileFallback.setDefaultValue(true);

    registerAspect(&specificConfigFile);
    specificConfigFile.setSettingsKey("specificConfigFile");

    registerAspect(&useSpecificConfigFile);
    useSpecificConfigFile.setSettingsKey("useSpecificConfigFile");

    read();
}

void UncrustifySettings::createDocumentationFile() const
{
    Process process;
    process.setTimeoutS(2);
    process.setCommand({command(), {"--show-config"}});
    process.runBlocking();
    if (process.result() != ProcessResult::FinishedWithSuccess)
        return;

    QFile file(documentationFilePath().toFSPathString());
    const QFileInfo fi(file);
    if (!fi.exists())
        fi.dir().mkpath(fi.absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;

    bool contextWritten = false;
    QXmlStreamWriter stream(&file);
    stream.setAutoFormatting(true);
    stream.writeStartDocument("1.0", true);
    stream.writeComment("Created " + QDateTime::currentDateTime().toString(Qt::ISODate));
    stream.writeStartElement(Constants::DOCUMENTATION_XMLROOT);

    const QStringList lines = process.allOutput().split(QLatin1Char('\n'));
    const int totalLines = lines.count();
    for (int i = 0; i < totalLines; ++i) {
        const QString &line = lines.at(i);
        if (line.startsWith('#') || line.trimmed().isEmpty())
            continue;

        const int firstSpace = line.indexOf(' ');
        const QString keyword = line.left(firstSpace);
        const QString options = line.right(line.size() - firstSpace).trimmed();
        QStringList docu;
        while (++i < totalLines) {
            const QString &subline = lines.at(i);
            if (line.startsWith('#') || subline.trimmed().isEmpty()) {
                const QString text = "<p><span class=\"option\">" + keyword
                        + "</span> <span class=\"param\">" + options
                        + "</span></p><p>" + docu.join(' ').toHtmlEscaped() + "</p>";
                stream.writeStartElement(Constants::DOCUMENTATION_XMLENTRY);
                stream.writeTextElement(Constants::DOCUMENTATION_XMLKEY, keyword);
                stream.writeTextElement(Constants::DOCUMENTATION_XMLDOC, text);
                stream.writeEndElement();
                contextWritten = true;
                break;
            } else {
                docu << subline;
            }
        }
    }

    stream.writeEndElement();
    stream.writeEndDocument();

    // An empty file causes error messages and a contextless file preventing this function to run
    // again in order to generate the documentation successfully. Thus delete the file.
    if (!contextWritten) {
        file.close();
        file.remove();
    }
}

} // Beautifier::Internal
