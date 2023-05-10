// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "artisticstylesettings.h"

#include "../beautifierconstants.h"

#include <coreplugin/icore.h>

#include <utils/genericconstants.h>
#include <utils/process.h>
#include <utils/stringutils.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamWriter>

using namespace Utils;

namespace Beautifier::Internal {

const char SETTINGS_NAME[]            = "artisticstyle";

ArtisticStyleSettings::ArtisticStyleSettings() :
    AbstractSettings(SETTINGS_NAME, ".astyle")
{
    setSettingsGroups(Utils::Constants::BEAUTIFIER_SETTINGS_GROUP, SETTINGS_NAME);
    setVersionRegExp(QRegularExpression("([2-9]{1})\\.([0-9]{1,2})(\\.[1-9]{1})?$"));
    setCommand("astyle");

    setDocumentationFilePath(Core::ICore::userResourcePath(Beautifier::Constants::SETTINGS_DIRNAME)
        .pathAppended(Beautifier::Constants::DOCUMENTATION_DIRNAME)
        .pathAppended(SETTINGS_NAME)
        .stringAppended(".xml"));

    registerAspect(&useOtherFiles);
    useOtherFiles.setSettingsKey("useOtherFiles");
    useOtherFiles.setDefaultValue(true);

    registerAspect(&useSpecificConfigFile);
    useSpecificConfigFile.setSettingsKey("useSpecificConfigFile");

    registerAspect(&specificConfigFile);
    specificConfigFile.setSettingsKey("specificConfigFile");

    registerAspect(&useHomeFile);
    useHomeFile.setSettingsKey("useHomeFile");

    registerAspect(&useCustomStyle);
    useCustomStyle.setSettingsKey("useCustomStyle");

    registerAspect(&customStyle);
    customStyle.setSettingsKey("customStyle");

    read();
}

void ArtisticStyleSettings::createDocumentationFile() const
{
    Process process;
    process.setTimeoutS(2);
    process.setCommand({command(), {"-h"}});
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

    // astyle writes its output to 'error'...
    const QStringList lines = process.cleanedStdErr().split(QLatin1Char('\n'));
    QStringList keys;
    QStringList docu;
    for (QString line : lines) {
        line = line.trimmed();
        if ((line.startsWith("--") && !line.startsWith("---")) || line.startsWith("OR ")) {
            const QStringList rawKeys = line.split(" OR ", Qt::SkipEmptyParts);
            for (QString k : rawKeys) {
                k = k.trimmed();
                k.remove('#');
                keys << k;
                if (k.startsWith("--"))
                    keys << k.right(k.size() - 2);
            }
        } else {
            if (line.isEmpty()) {
                if (!keys.isEmpty()) {
                    // Write entry
                    stream.writeStartElement(Constants::DOCUMENTATION_XMLENTRY);
                    stream.writeStartElement(Constants::DOCUMENTATION_XMLKEYS);
                    for (const QString &key : std::as_const(keys))
                        stream.writeTextElement(Constants::DOCUMENTATION_XMLKEY, key);
                    stream.writeEndElement();
                    const QString text = "<p><span class=\"option\">"
                            + keys.filter(QRegularExpression("^\\-")).join(", ") + "</span></p><p>"
                            + (docu.join(' ').toHtmlEscaped()) + "</p>";
                    stream.writeTextElement(Constants::DOCUMENTATION_XMLDOC, text);
                    stream.writeEndElement();
                    contextWritten = true;
                }
                keys.clear();
                docu.clear();
            } else if (!keys.isEmpty()) {
                docu << line;
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
