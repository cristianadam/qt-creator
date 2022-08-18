// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "codestyleselectorwidget.h"

#include "icodestylepreferences.h"
#include "icodestylepreferencesfactory.h"
#include "codestylepool.h"
#include "tabsettings.h"

#include <utils/fileutils.h>
#include <utils/layoutbuilder.h>

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

using namespace TextEditor;
using namespace Utils;

namespace TextEditor {
namespace Internal {

class CodeStyleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CodeStyleDialog(ICodeStylePreferencesFactory *factory,
                             ICodeStylePreferences *codeStyle,
                             ProjectExplorer::Project *project = nullptr,
                             QWidget *parent = nullptr);
    ~CodeStyleDialog() override;
    ICodeStylePreferences *codeStyle() const;
private:
    void slotCopyClicked();
    void slotDisplayNameChanged();

    ICodeStylePreferences *m_codeStyle;
    QLineEdit *m_lineEdit;
    QDialogButtonBox *m_buttons;
    QLabel *m_warningLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QString m_originalDisplayName;
};

CodeStyleDialog::CodeStyleDialog(ICodeStylePreferencesFactory *factory,
                                 ICodeStylePreferences *codeStyle,
                                 ProjectExplorer::Project *project,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit Code Style"));
    auto layout = new QVBoxLayout(this);
    QLabel *label = new QLabel(tr("Code style name:"));
    m_lineEdit = new QLineEdit(codeStyle->displayName(), this);
    auto nameLayout = new QHBoxLayout;
    nameLayout->addWidget(label);
    nameLayout->addWidget(m_lineEdit);
    layout->addLayout(nameLayout);

    if (codeStyle->isReadOnly()) {
        auto warningLayout = new QHBoxLayout;
        m_warningLabel = new QLabel(
                    tr("You cannot save changes to a built-in code style. "
                       "Copy it first to create your own version."), this);
        QFont font = m_warningLabel->font();
        font.setItalic(true);
        m_warningLabel->setFont(font);
        m_warningLabel->setWordWrap(true);
        m_copyButton = new QPushButton(tr("Copy Built-in Code Style"), this);
        m_copyButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(m_copyButton, &QAbstractButton::clicked, this, &CodeStyleDialog::slotCopyClicked);
        warningLayout->addWidget(m_warningLabel);
        warningLayout->addWidget(m_copyButton);
        layout->addLayout(warningLayout);
    }

    m_originalDisplayName = codeStyle->displayName();
    m_codeStyle = factory->createCodeStyle();
    m_codeStyle->setTabSettings(codeStyle->tabSettings());
    m_codeStyle->setValue(codeStyle->value());
    m_codeStyle->setId(codeStyle->id());
    m_codeStyle->setDisplayName(m_originalDisplayName);
    m_codeStyle->setReadOnly(codeStyle->isReadOnly());
    CodeStyleEditorWidget *editor = factory->createEditor(m_codeStyle, project, this);

    m_buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    if (codeStyle->isReadOnly()) {
        QPushButton *okButton = m_buttons->button(QDialogButtonBox::Ok);
        okButton->setEnabled(false);
    }

    if (editor)
        layout->addWidget(editor);
    layout->addWidget(m_buttons);
    resize(850, 600);

    connect(m_lineEdit, &QLineEdit::textChanged, this, &CodeStyleDialog::slotDisplayNameChanged);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::accepted, editor, &TextEditor::CodeStyleEditorWidget::apply);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ICodeStylePreferences *CodeStyleDialog::codeStyle() const
{
    return m_codeStyle;
}

void CodeStyleDialog::slotCopyClicked()
{
    if (m_warningLabel)
        m_warningLabel->hide();
    if (m_copyButton)
        m_copyButton->hide();
    QPushButton *okButton = m_buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(true);
    if (m_lineEdit->text() == m_originalDisplayName)
        m_lineEdit->setText(tr("%1 (Copy)").arg(m_lineEdit->text()));
    m_lineEdit->selectAll();
}

void CodeStyleDialog::slotDisplayNameChanged()
{
    m_codeStyle->setDisplayName(m_lineEdit->text());
}

CodeStyleDialog::~CodeStyleDialog()
{
    delete m_codeStyle;
}

} // Internal

CodeStyleSelectorWidget::CodeStyleSelectorWidget(ICodeStylePreferencesFactory *factory,
                                                 ProjectExplorer::Project *project,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_factory(factory)
    , m_project(project)
{
    resize(536, 59);

    m_delegateComboBox = new QComboBox(this);
    m_delegateComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto copyButton = new QPushButton(tr("Copy..."));
    auto editButton = new QPushButton(tr("Edit..."));

    m_removeButton = new QPushButton(tr("Remove"));

    m_exportButton = new QPushButton(tr("Export..."));
    m_exportButton->setEnabled(false);

    m_importButton = new QPushButton(tr("Import..."));
    m_importButton->setEnabled(false);

    using namespace Utils::Layouting;

    Column {
        Grid {
            tr("Current settings:"),
            m_delegateComboBox,
            copyButton,
            editButton,
            m_removeButton,
            m_exportButton,
            br,

            Span(5, Space(1)),
            m_importButton
        },

    }.attachTo(this, WithoutMargins);

    connect(m_delegateComboBox, &QComboBox::activated,
            this, &CodeStyleSelectorWidget::slotComboBoxActivated);
    connect(copyButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotCopyClicked);
    connect(editButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotEditClicked);
    connect(m_removeButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotRemoveClicked);
    connect(m_importButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotImportClicked);
    connect(m_exportButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotExportClicked);
}

CodeStyleSelectorWidget::~CodeStyleSelectorWidget() = default;

void CodeStyleSelectorWidget::setCodeStyle(ICodeStylePreferences *codeStyle)
{
    if (m_codeStyle == codeStyle)
        return; // nothing changes

    // cleanup old
    if (m_codeStyle) {
        CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
        if (codeStylePool) {
            disconnect(codeStylePool, &CodeStylePool::codeStyleAdded,
                    this, &CodeStyleSelectorWidget::slotCodeStyleAdded);
            disconnect(codeStylePool, &CodeStylePool::codeStyleRemoved,
                    this, &CodeStyleSelectorWidget::slotCodeStyleRemoved);
        }
        disconnect(m_codeStyle, &ICodeStylePreferences::currentDelegateChanged,
                this, &CodeStyleSelectorWidget::slotCurrentDelegateChanged);

        m_exportButton->setEnabled(false);
        m_importButton->setEnabled(false);
        m_delegateComboBox->clear();
    }
    m_codeStyle = codeStyle;
    // fillup new
    if (m_codeStyle) {
        QList<ICodeStylePreferences *> delegates;
        CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
        if (codeStylePool) {
            delegates = codeStylePool->codeStyles();

            connect(codeStylePool, &CodeStylePool::codeStyleAdded,
                    this, &CodeStyleSelectorWidget::slotCodeStyleAdded);
            connect(codeStylePool, &CodeStylePool::codeStyleRemoved,
                    this, &CodeStyleSelectorWidget::slotCodeStyleRemoved);
            m_exportButton->setEnabled(true);
            m_importButton->setEnabled(true);
        }

        for (int i = 0; i < delegates.count(); i++)
            slotCodeStyleAdded(delegates.at(i));

        slotCurrentDelegateChanged(m_codeStyle->currentDelegate());

        connect(m_codeStyle, &ICodeStylePreferences::currentDelegateChanged,
                this, &CodeStyleSelectorWidget::slotCurrentDelegateChanged);
    }
}

void CodeStyleSelectorWidget::slotComboBoxActivated(int index)
{
    if (m_ignoreChanges.isLocked())
        return;

    if (index < 0 || index >= m_delegateComboBox->count())
        return;
    auto delegate = m_delegateComboBox->itemData(index).value<ICodeStylePreferences *>();

    QSignalBlocker blocker(this);
    m_codeStyle->setCurrentDelegate(delegate);
}

void CodeStyleSelectorWidget::slotCurrentDelegateChanged(ICodeStylePreferences *delegate)
{
    {
        const GuardLocker locker(m_ignoreChanges);
        m_delegateComboBox->setCurrentIndex(m_delegateComboBox->findData(QVariant::fromValue(delegate)));
        m_delegateComboBox->setToolTip(m_delegateComboBox->currentText());
    }

    const bool removeEnabled = delegate && !delegate->isReadOnly() && !delegate->currentDelegate();
    m_removeButton->setEnabled(removeEnabled);
}

void CodeStyleSelectorWidget::slotCopyClicked()
{
    if (!m_codeStyle)
        return;

    CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
    ICodeStylePreferences *currentPreferences = m_codeStyle->currentPreferences();
    bool ok = false;
    const QString newName = QInputDialog::getText(this,
                                                  tr("Copy Code Style"),
                                                  tr("Code style name:"),
                                                  QLineEdit::Normal,
                                                  tr("%1 (Copy)").arg(currentPreferences->displayName()),
                                                  &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;
    ICodeStylePreferences *copy = codeStylePool->cloneCodeStyle(currentPreferences);
    if (copy) {
        copy->setDisplayName(newName);
        m_codeStyle->setCurrentDelegate(copy);
    }
}

void CodeStyleSelectorWidget::slotEditClicked()
{
    if (!m_codeStyle)
        return;

    ICodeStylePreferences *codeStyle = m_codeStyle->currentPreferences();
    // check if it's read-only

    Internal::CodeStyleDialog dialog(m_factory, codeStyle, m_project, this);
    if (dialog.exec() == QDialog::Accepted) {
        ICodeStylePreferences *dialogCodeStyle = dialog.codeStyle();
        if (codeStyle->isReadOnly()) {
            CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
            codeStyle = codeStylePool->cloneCodeStyle(dialogCodeStyle);
            if (codeStyle)
                m_codeStyle->setCurrentDelegate(codeStyle);
            return;
        }
        codeStyle->setTabSettings(dialogCodeStyle->tabSettings());
        codeStyle->setValue(dialogCodeStyle->value());
        codeStyle->setDisplayName(dialogCodeStyle->displayName());
    }
}

void CodeStyleSelectorWidget::slotRemoveClicked()
{
    if (!m_codeStyle)
        return;

    CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
    ICodeStylePreferences *currentPreferences = m_codeStyle->currentPreferences();

    QMessageBox messageBox(QMessageBox::Warning,
                           tr("Delete Code Style"),
                           tr("Are you sure you want to delete this code style permanently?"),
                           QMessageBox::Discard | QMessageBox::Cancel,
                           this);

    // Change the text and role of the discard button
    auto deleteButton = static_cast<QPushButton*>(messageBox.button(QMessageBox::Discard));
    deleteButton->setText(tr("Delete"));
    messageBox.addButton(deleteButton, QMessageBox::AcceptRole);
    messageBox.setDefaultButton(deleteButton);

    connect(deleteButton, &QAbstractButton::clicked, &messageBox, &QDialog::accept);
    if (messageBox.exec() == QDialog::Accepted)
        codeStylePool->removeCodeStyle(currentPreferences);
}

void CodeStyleSelectorWidget::slotImportClicked()
{
    const FilePath fileName =
            FileUtils::getOpenFilePath(this, tr("Import Code Style"), {},
                                       tr("Code styles (*.xml);;All files (*)"));
    if (!fileName.isEmpty()) {
        CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
        ICodeStylePreferences *importedStyle = codeStylePool->importCodeStyle(fileName);
        if (importedStyle)
            m_codeStyle->setCurrentDelegate(importedStyle);
        else
            QMessageBox::warning(this, tr("Import Code Style"),
                                 tr("Cannot import code style from %1"), fileName.toUserOutput());
    }
}

void CodeStyleSelectorWidget::slotExportClicked()
{
    ICodeStylePreferences *currentPreferences = m_codeStyle->currentPreferences();
    const FilePath filePath = FileUtils::getSaveFilePath(this, tr("Export Code Style"),
                             FilePath::fromString(QString::fromUtf8(currentPreferences->id() + ".xml")),
                             tr("Code styles (*.xml);;All files (*)"));
    if (!filePath.isEmpty()) {
        CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
        codeStylePool->exportCodeStyle(filePath, currentPreferences);
    }
}

void CodeStyleSelectorWidget::slotCodeStyleAdded(ICodeStylePreferences *codeStylePreferences)
{
    if (codeStylePreferences == m_codeStyle
            || codeStylePreferences->id() == m_codeStyle->id())
        return;

    const QVariant data = QVariant::fromValue(codeStylePreferences);
    const QString name = displayName(codeStylePreferences);
    m_delegateComboBox->addItem(name, data);
    m_delegateComboBox->setItemData(m_delegateComboBox->count() - 1, name, Qt::ToolTipRole);
    connect(codeStylePreferences, &ICodeStylePreferences::displayNameChanged,
            this, [this, codeStylePreferences] { slotUpdateName(codeStylePreferences); });
    if (codeStylePreferences->delegatingPool()) {
        connect(codeStylePreferences, &ICodeStylePreferences::currentPreferencesChanged,
                this, [this, codeStylePreferences] { slotUpdateName(codeStylePreferences); });
    }
}

void CodeStyleSelectorWidget::slotCodeStyleRemoved(ICodeStylePreferences *codeStylePreferences)
{
    const GuardLocker locker(m_ignoreChanges);
    m_delegateComboBox->removeItem(m_delegateComboBox->findData(
                                           QVariant::fromValue(codeStylePreferences)));
    disconnect(codeStylePreferences, &ICodeStylePreferences::displayNameChanged, this, nullptr);
    if (codeStylePreferences->delegatingPool()) {
        disconnect(codeStylePreferences, &ICodeStylePreferences::currentPreferencesChanged,
                   this, nullptr);
    }
}

void CodeStyleSelectorWidget::slotUpdateName(ICodeStylePreferences *codeStylePreferences)
{
    updateName(codeStylePreferences);

    QList<ICodeStylePreferences *> codeStyles = m_codeStyle->delegatingPool()->codeStyles();
    for (int i = 0; i < codeStyles.count(); i++) {
        ICodeStylePreferences *codeStyle = codeStyles.at(i);
        if (codeStyle->currentDelegate() == codeStylePreferences)
            updateName(codeStyle);
    }

    m_delegateComboBox->setToolTip(m_delegateComboBox->currentText());
}

void CodeStyleSelectorWidget::updateName(ICodeStylePreferences *codeStyle)
{
    const int idx = m_delegateComboBox->findData(QVariant::fromValue(codeStyle));
    if (idx < 0)
        return;

    const QString name = displayName(codeStyle);
    m_delegateComboBox->setItemText(idx, name);
    m_delegateComboBox->setItemData(idx, name, Qt::ToolTipRole);
}

QString CodeStyleSelectorWidget::displayName(ICodeStylePreferences *codeStyle) const
{
    QString name = codeStyle->displayName();
    if (codeStyle->currentDelegate())
        name = tr("%1 [proxy: %2]").arg(name).arg(codeStyle->currentDelegate()->displayName());
    if (codeStyle->isReadOnly())
        name = tr("%1 [built-in]").arg(name);
    return name;
}

} // TextEditor

#include "codestyleselectorwidget.moc"
