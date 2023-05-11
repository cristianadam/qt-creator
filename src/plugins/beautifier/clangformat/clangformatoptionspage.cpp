// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangformatoptionspage.h"

#include "clangformatsettings.h"

#include "../beautifierconstants.h"
#include "../beautifierplugin.h"
#include "../beautifiertr.h"
#include "../configurationpanel.h"

#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

#include <QButtonGroup>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpacerItem>

namespace Beautifier::Internal {

class ClangFormatOptionsPageWidget : public Core::IOptionsPageWidget
{
public:
    explicit ClangFormatOptionsPageWidget(ClangFormatSettings *settings);

    void apply() final;

private:
    ClangFormatSettings *m_settings;
    ConfigurationPanel *m_configurations;
    QRadioButton *m_usePredefinedStyle;
    QComboBox *m_predefinedStyle;
    QComboBox *m_fallbackStyle;
    Utils::PathChooser *m_command;
    QLineEdit *m_mime;
};

ClangFormatOptionsPageWidget::ClangFormatOptionsPageWidget(ClangFormatSettings *settings)
    : m_settings(settings)
{
    auto options = new QGroupBox(Tr::tr("Options"));
    options->setEnabled(false);

    auto styleButtonGroup = new QButtonGroup(this);

    auto useCustomizedStyle = new QRadioButton(Tr::tr("Use customized style:"));
    styleButtonGroup->addButton(useCustomizedStyle);

    m_configurations = new ConfigurationPanel;
    m_configurations->setSettings(m_settings);
    m_configurations->setCurrentConfiguration(m_settings->customStyle.value());

    m_usePredefinedStyle = new QRadioButton(Tr::tr("Use predefined style:"));

    m_usePredefinedStyle->setChecked(true);
    styleButtonGroup->addButton(m_usePredefinedStyle);

    m_predefinedStyle = new QComboBox;
    m_predefinedStyle->addItems(m_settings->predefinedStyles());
    const int predefinedStyleIndex = m_predefinedStyle->findText(m_settings->predefinedStyle.value());
    if (predefinedStyleIndex != -1)
        m_predefinedStyle->setCurrentIndex(predefinedStyleIndex);

    m_fallbackStyle = new QComboBox;
    m_fallbackStyle->addItems(m_settings->fallbackStyles());
    m_fallbackStyle->setEnabled(false);
    const int fallbackStyleIndex = m_fallbackStyle->findText(m_settings->fallbackStyle.value());
    if (fallbackStyleIndex != -1)
        m_fallbackStyle->setCurrentIndex(fallbackStyleIndex);

    m_mime = new QLineEdit(m_settings->supportedMimeTypesAsString());

    m_command = new Utils::PathChooser;
    m_command->setExpectedKind(Utils::PathChooser::ExistingCommand);
    m_command->setCommandVersionArguments({"--version"});
    m_command->setPromptDialogTitle(
                BeautifierPlugin::msgCommandPromptDialogTitle("Clang Format"));

    if (m_settings->usePredefinedStyle.value())
        m_usePredefinedStyle->setChecked(true);
    else
        useCustomizedStyle->setChecked(true);

    using namespace Layouting;

    Form {
        m_usePredefinedStyle, m_predefinedStyle, br,
        empty, Row { Tr::tr("Fallback style:"), m_fallbackStyle }, br,
        useCustomizedStyle, m_configurations, br,
    }.attachTo(options);

    Column {
        Group {
            title(Tr::tr("Configuration")),
            Form {
                Tr::tr("Clang Format command:"), m_command, br,
                Tr::tr("Restrict to MIME types:"), m_mime
            }
        },
        options,
        st
    }.attachTo(this);

    connect(m_command, &Utils::PathChooser::validChanged, options, &QWidget::setEnabled);
    connect(m_predefinedStyle, &QComboBox::currentTextChanged, this, [this](const QString &item) {
        m_fallbackStyle->setEnabled(item == "File");
    });
    connect(m_usePredefinedStyle, &QRadioButton::toggled, this, [this](bool checked) {
        m_fallbackStyle->setEnabled(checked && m_predefinedStyle->currentText() == "File");
        m_predefinedStyle->setEnabled(checked);
    });

    // might trigger PathChooser::validChanged, so so after the connect above
    m_command->setFilePath(m_settings->command());
}

void ClangFormatOptionsPageWidget::apply()
{
    m_settings->setCommand(m_command->filePath());
    m_settings->setSupportedMimeTypes(m_mime->text());
    m_settings->usePredefinedStyle.setValue(m_usePredefinedStyle->isChecked());
    m_settings->predefinedStyle.setValue(m_predefinedStyle->currentText());
    m_settings->fallbackStyle.setValue(m_fallbackStyle->currentText());
    m_settings->customStyle.setValue(m_configurations->currentConfiguration());
    m_settings->save();

    // update since not all MIME types are accepted (invalids or duplicates)
    m_mime->setText(m_settings->supportedMimeTypesAsString());
}

ClangFormatOptionsPage::ClangFormatOptionsPage(ClangFormatSettings *settings)
{
    setId("ClangFormat");
    setDisplayName(Tr::tr("Clang Format"));
    setCategory(Constants::OPTION_CATEGORY);
    setWidgetCreator([settings] { return new ClangFormatOptionsPageWidget(settings); });
}

} // Beautifier::Internal
