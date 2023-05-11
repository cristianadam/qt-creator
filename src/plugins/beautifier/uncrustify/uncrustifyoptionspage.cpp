// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "uncrustifyoptionspage.h"

#include "uncrustifyconstants.h"
#include "uncrustifysettings.h"

#include "../beautifierconstants.h"
#include "../beautifierplugin.h"
#include "../beautifiertr.h"
#include "../configurationpanel.h"

#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>

namespace Beautifier::Internal {

class UncrustifyOptionsPageWidget : public Core::IOptionsPageWidget
{
public:
    explicit UncrustifyOptionsPageWidget(UncrustifySettings *settings);

    void apply() final;

private:
    UncrustifySettings *m_settings;

    Utils::PathChooser *m_command;
    QLineEdit *m_mime;
    QCheckBox *m_useOtherFiles;
    QCheckBox *m_useSpecificFile;
    Utils::PathChooser *m_uncrusifyFilePath;
    QCheckBox *m_useHomeFile;
    QCheckBox *m_useCustomStyle;
    ConfigurationPanel *m_configurations;
    QCheckBox *m_formatEntireFileFallback;
};

UncrustifyOptionsPageWidget::UncrustifyOptionsPageWidget(UncrustifySettings *settings)
    : m_settings(settings)
{
    m_command = new Utils::PathChooser;

    m_mime = new QLineEdit(m_settings->supportedMimeTypesAsString());

    m_useOtherFiles = new QCheckBox(Tr::tr("Use file uncrustify.cfg defined in project files"));
    m_useOtherFiles->setChecked(m_settings->useOtherFiles.value());

    m_useSpecificFile = new QCheckBox(Tr::tr("Use file specific uncrustify.cfg"));
    m_useSpecificFile->setChecked(m_settings->useSpecificConfigFile.value());

    m_uncrusifyFilePath = new Utils::PathChooser;
    m_uncrusifyFilePath->setExpectedKind(Utils::PathChooser::File);
    m_uncrusifyFilePath->setPromptDialogFilter(Tr::tr("Uncrustify file (*.cfg)"));
    m_uncrusifyFilePath->setFilePath(m_settings->specificConfigFile.filePath());

    m_useHomeFile = new QCheckBox(Tr::tr("Use file uncrustify.cfg in HOME")
        .replace( "HOME", QDir::toNativeSeparators(QDir::home().absolutePath())));
    m_useHomeFile->setChecked(m_settings->useHomeFile.value());

    m_useCustomStyle = new QCheckBox(Tr::tr("Use customized style:"));
    m_useCustomStyle->setChecked(m_settings->useCustomStyle.value());

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

    using namespace Layouting;

    Column {
        m_useOtherFiles,
        Row { m_useSpecificFile, m_uncrusifyFilePath },
        m_useHomeFile,
        Row { m_useCustomStyle, m_configurations },
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
    }.attachTo(this);

    connect(m_command, &Utils::PathChooser::validChanged, options, &QWidget::setEnabled);
}

void UncrustifyOptionsPageWidget::apply()
{
    m_settings->setCommand(m_command->filePath());
    m_settings->setSupportedMimeTypes(m_mime->text());
    m_settings->useOtherFiles.setValue(m_useOtherFiles->isChecked());
    m_settings->useHomeFile.setValue(m_useHomeFile->isChecked());
    m_settings->useSpecificConfigFile.setValue(m_useSpecificFile->isChecked());
    m_settings->specificConfigFile.setFilePath(m_uncrusifyFilePath->filePath());
    m_settings->useCustomStyle.setValue(m_useCustomStyle->isChecked());
    m_settings->customStyle.setValue(m_configurations->currentConfiguration());
    m_settings->formatEntireFileFallback.setValue(m_formatEntireFileFallback->isChecked());
    m_settings->save();

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
