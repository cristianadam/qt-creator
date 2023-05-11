// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "uncrustifysettings.h"

#include "uncrustifyconstants.h"

#include "../beautifierconstants.h"
#include "../beautifierplugin.h"
#include "../beautifiertr.h"
#include "../configurationpanel.h"

#include <coreplugin/icore.h>

#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/process.h>

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamWriter>

using namespace Utils;

namespace Beautifier::Internal {

const char SETTINGS_NAME[]                 = "uncrustify";

UncrustifySettings::UncrustifySettings()
    : AbstractSettings(SETTINGS_NAME, ".cfg")
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
    useOtherFiles.setLabelText(Tr::tr("Use file uncrustify.cfg defined in project files"));

    registerAspect(&useHomeFile);
    useHomeFile.setSettingsKey("useHomeFile");
    useHomeFile.setLabelText(Tr::tr("Use file uncrustify.cfg in HOME")
        .replace("HOME", QDir::toNativeSeparators(QDir::home().absolutePath())));

    registerAspect(&useCustomStyle);
    useCustomStyle.setSettingsKey("useCustomStyle");
    useCustomStyle.setLabelText(Tr::tr("Use customized style:"));

    registerAspect(&customStyle);
    customStyle.setSettingsKey("customStyle");

    registerAspect(&formatEntireFileFallback);
    formatEntireFileFallback.setSettingsKey("formatEntireFileFallback");
    formatEntireFileFallback.setDefaultValue(true);

    registerAspect(&specificConfigFile);
    specificConfigFile.setSettingsKey("specificConfigFile");
    specificConfigFile.setExpectedKind(Utils::PathChooser::File);
    specificConfigFile.setPromptDialogFilter(Tr::tr("Uncrustify file (*.cfg)"));

    registerAspect(&useSpecificConfigFile);
    useSpecificConfigFile.setSettingsKey("useSpecificConfigFile");
    useSpecificConfigFile.setLabelText(Tr::tr("Use file specific uncrustify.cfg"));

    read();

    Utils::PathChooser *m_command;
    QLineEdit *m_mime;
    ConfigurationPanel *m_configurations;
    QCheckBox *m_formatEntireFileFallback;
;
    m_command = new Utils::PathChooser;

    m_mime = new QLineEdit(m_settings->supportedMimeTypesAsString());


    m_configurations = new ConfigurationPanel;
    m_configurations->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_configurations->setSettings(m_settings);
    m_configurations->setCurrentConfiguration(m_settings->customStyle.value());

    m_formatEntireFileFallback = new QCheckBox(Tr::tr("Format entire file if no text was selected"));
    m_formatEntireFileFallback->setToolTip(Tr::tr("For action Format Selected Text"));
    m_formatEntireFileFallback->setChecked(m_settings->formatEntireFileFallback.value());

    m_command->setExpectedKind(Utils::PathChooser::ExistingCommand);
    m_command->setCommandVersionArguments({"--version"});
    m_command->setPromptDialogTitle(BeautifierPlugin::msgCommandPromptDialogTitle(
                                          Tr::tr(Constants::UNCRUSTIFY_DISPLAY_NAME)));
    m_command->setFilePath(m_settings->command());

    auto options = new QGroupBox(Tr::tr("Options"));

    setLayouter([this](QWidget *widget) {
        using namespace Layouting;

        Column {
            useOtherFiles,
            Row { useSpecificConfigFile, specificConfigFile },
            useHomeFile,
            Row { useCustomStyle, m_configurations },
            m_formatEntireFileFallback
        }.attachTo(options);

        Column {
            Group {
                title(Tr::tr("Configuration")),
                Form {
                    Tr::tr("Uncrustify command:"), m_command, br,
                    Tr::tr("Restrict to MIME types:"), m_mime
                }
            },
            options,
            st
        }.attachTo(widget);
    });

    connect(m_command, &Utils::PathChooser::validChanged, options, &QWidget::setEnabled);

    // update since not all MIME types are accepted (invalids or duplicates)
    m_mime->setText(m_settings->supportedMimeTypesAsString());
}

UncrustifyOptionsPage::UncrustifyOptionsPage(UncrustifySettings *settings)
{
    setId("Uncrustify");
    setDisplayName(Tr::tr("Uncrustify"));
    setCategory(Constants::OPTION_CATEGORY);
    setWidgetCreator([settings] { return new UncrustifyOptionsPageWidget(settings); });
}

} // Beautifier::Internal
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
