// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "gitsubmiteditorwidget.h"

#include "commitdata.h"
#include "githighlighters.h"
#include "logchangedialog.h"

#include <coreplugin/coreconstants.h>

#include <utils/completingtextedit.h>
#include <utils/filepath.h>
#include <utils/layoutbuilder.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QGroupBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpressionValidator>
#include <QTextEdit>
#include <QVBoxLayout>

using namespace Utils;

namespace Git {
namespace Internal {

class GitSubmitPanel : public QWidget
{
public:
    GitSubmitPanel()
    {
        resize(364, 269);

        repositoryLabel = new QLabel(tr("repository"));
        branchLabel = new QLabel(tr("branch")); // FIXME: Isn't this overwritten soon?
        showHeadLabel = new QLabel(tr("<a href=\"head\">Show HEAD</a>")); // FIXME: Simplify string in tr()

        authorLineEdit = new QLineEdit;
        authorLineEdit->setMinimumSize(QSize(200, 0));

        invalidAuthorLabel = new QLabel;
        invalidAuthorLabel->setMinimumSize(QSize(20, 20));

        emailLineEdit = new QLineEdit;
        emailLineEdit->setMinimumSize(QSize(200, 0));

        invalidEmailLabel = new QLabel;
        invalidEmailLabel->setMinimumSize(QSize(20, 20));

        bypassHooksCheckBox = new QCheckBox(tr("By&pass hooks"));

        signOffCheckBox = new QCheckBox(tr("Sign off"));

        using namespace Layouting;

        editGroup = new QGroupBox(tr("Commit Information"));
        Grid {
            tr("Author:"), authorLineEdit, invalidAuthorLabel, st, br,
            tr("Email:"), emailLineEdit, invalidEmailLabel, br,
            empty, Row { bypassHooksCheckBox, signOffCheckBox, st }
        }.attachTo(editGroup);

        Column {
            Group {
                title(tr("General Information")),
                Form {
                    tr("Repository:"), repositoryLabel, br,
                    tr("Branch:"), branchLabel, br,
                    Span(2, showHeadLabel)
                }
            },
            editGroup,
        }.attachTo(this, WithoutMargins);
    }

    QLabel *repositoryLabel;
    QLabel *branchLabel;
    QLabel *showHeadLabel;
    QGroupBox *editGroup;
    QLineEdit *authorLineEdit;
    QLabel *invalidAuthorLabel;
    QLineEdit *emailLineEdit;
    QLabel *invalidEmailLabel;
    QCheckBox *bypassHooksCheckBox;
    QCheckBox *signOffCheckBox;
};

// ------------------
GitSubmitEditorWidget::GitSubmitEditorWidget() :
    m_gitSubmitPanel(new GitSubmitPanel)
{
    new GitSubmitHighlighter(descriptionEdit());

    m_emailValidator = new QRegularExpressionValidator(QRegularExpression("[^@ ]+@[^@ ]+\\.[a-zA-Z]+"), this);
    const QPixmap error = Utils::Icons::CRITICAL.pixmap();
    m_gitSubmitPanel->invalidAuthorLabel->setPixmap(error);
    m_gitSubmitPanel->invalidEmailLabel->setToolTip(tr("Provide a valid email to commit."));
    m_gitSubmitPanel->invalidEmailLabel->setPixmap(error);

    connect(m_gitSubmitPanel->authorLineEdit, &QLineEdit::textChanged,
            this, &GitSubmitEditorWidget::authorInformationChanged);
    connect(m_gitSubmitPanel->emailLineEdit, &QLineEdit::textChanged,
            this, &GitSubmitEditorWidget::authorInformationChanged);
    connect(m_gitSubmitPanel->showHeadLabel, &QLabel::linkActivated,
            this, [this] { emit showRequested("HEAD"); });
}

void GitSubmitEditorWidget::setPanelInfo(const GitSubmitEditorPanelInfo &info)
{
    m_gitSubmitPanel->repositoryLabel->setText(info.repository.toUserOutput());
    if (info.branch.contains("(no branch)")) {
        const QString errorColor =
                Utils::creatorTheme()->color(Utils::Theme::TextColorError).name();
        m_gitSubmitPanel->branchLabel->setText(QString::fromLatin1("<span style=\"color:%1\">%2</span>")
                                                .arg(errorColor, tr("Detached HEAD")));
    } else {
        m_gitSubmitPanel->branchLabel->setText(info.branch);
    }
}

QString GitSubmitEditorWidget::amendSHA1() const
{
    return m_logChangeWidget ? m_logChangeWidget->commit() : QString();
}

void GitSubmitEditorWidget::setHasUnmerged(bool e)
{
    m_hasUnmerged = e;
}

void GitSubmitEditorWidget::initialize(CommitType commitType,
                                       const FilePath &repository,
                                       const GitSubmitEditorPanelData &data,
                                       const GitSubmitEditorPanelInfo &info,
                                       bool enablePush)
{
    if (m_isInitialized)
        return;
    m_isInitialized = true;
    if (commitType != AmendCommit)
        m_gitSubmitPanel->showHeadLabel->hide();
    if (commitType == FixupCommit) {
        auto logChangeGroupBox = new QGroupBox(tr("Select Change"));
        auto logChangeLayout = new QVBoxLayout;
        logChangeGroupBox->setLayout(logChangeLayout);
        m_logChangeWidget = new LogChangeWidget;
        m_logChangeWidget->init(repository);
        connect(m_logChangeWidget, &LogChangeWidget::commitActivated, this, &GitSubmitEditorWidget::showRequested);
        logChangeLayout->addWidget(m_logChangeWidget);
        insertLeftWidget(logChangeGroupBox);
        m_gitSubmitPanel->editGroup->hide();
        hideDescription();
    }
    insertTopWidget(m_gitSubmitPanel);
    setPanelData(data);
    setPanelInfo(info);

    if (enablePush) {
        auto menu = new QMenu(this);
        connect(menu->addAction(tr("&Commit only")), &QAction::triggered,
                this, &GitSubmitEditorWidget::commitOnlySlot);
        connect(menu->addAction(tr("Commit and &Push")), &QAction::triggered,
                this, &GitSubmitEditorWidget::commitAndPushSlot);
        connect(menu->addAction(tr("Commit and Push to &Gerrit")), &QAction::triggered,
                this, &GitSubmitEditorWidget::commitAndPushToGerritSlot);
        addSubmitButtonMenu(menu);
    }
}

void GitSubmitEditorWidget::refreshLog(const FilePath &repository)
{
    if (m_logChangeWidget)
        m_logChangeWidget->init(repository);
}

GitSubmitEditorPanelData GitSubmitEditorWidget::panelData() const
{
    GitSubmitEditorPanelData rc;
    const QString author = m_gitSubmitPanel->authorLineEdit->text();
    const QString email = m_gitSubmitPanel->emailLineEdit->text();
    if (author != m_originalAuthor || email != m_originalEmail) {
        rc.author = author;
        rc.email = email;
    }
    rc.bypassHooks = m_gitSubmitPanel->bypassHooksCheckBox->isChecked();
    rc.pushAction = m_pushAction;
    rc.signOff = m_gitSubmitPanel->signOffCheckBox->isChecked();
    return rc;
}

void GitSubmitEditorWidget::setPanelData(const GitSubmitEditorPanelData &data)
{
    m_originalAuthor = data.author;
    m_originalEmail = data.email;
    m_gitSubmitPanel->authorLineEdit->setText(data.author);
    m_gitSubmitPanel->emailLineEdit->setText(data.email);
    m_gitSubmitPanel->bypassHooksCheckBox->setChecked(data.bypassHooks);
    m_gitSubmitPanel->signOffCheckBox->setChecked(data.signOff);
    authorInformationChanged();
}

bool GitSubmitEditorWidget::canSubmit(QString *whyNot) const
{
    if (m_gitSubmitPanel->invalidAuthorLabel->isVisible()) {
        if (whyNot)
            *whyNot = tr("Invalid author");
        return false;
    }
    if (m_gitSubmitPanel->invalidEmailLabel->isVisible()) {
        if (whyNot)
            *whyNot = tr("Invalid email");
        return false;
    }
    if (m_hasUnmerged) {
        if (whyNot)
            *whyNot = tr("Unresolved merge conflicts");
        return false;
    }
    return SubmitEditorWidget::canSubmit(whyNot);
}

QString GitSubmitEditorWidget::cleanupDescription(const QString &input) const
{
    // We need to manually purge out comment lines starting with
    // hash '#' since git does not do that when using -F.
    const QChar newLine = '\n';
    const QChar hash = '#';
    QString message = input;
    for (int pos = 0; pos < message.size(); ) {
        const int newLinePos = message.indexOf(newLine, pos);
        const int startOfNextLine = newLinePos == -1 ? message.size() : newLinePos + 1;
        if (message.at(pos) == hash)
            message.remove(pos, startOfNextLine - pos);
        else
            pos = startOfNextLine;
    }
    return message;

}

QString GitSubmitEditorWidget::commitName() const
{
    if (m_pushAction == NormalPush)
        return tr("&Commit and Push");
    else if (m_pushAction == PushToGerrit)
        return tr("&Commit and Push to Gerrit");

    return tr("&Commit");
}

void GitSubmitEditorWidget::authorInformationChanged()
{
    bool bothEmpty = m_gitSubmitPanel->authorLineEdit->text().isEmpty() &&
            m_gitSubmitPanel->emailLineEdit->text().isEmpty();

    m_gitSubmitPanel->invalidAuthorLabel->
            setVisible(m_gitSubmitPanel->authorLineEdit->text().isEmpty() && !bothEmpty);
    m_gitSubmitPanel->invalidEmailLabel->
            setVisible(!emailIsValid() && !bothEmpty);

    updateSubmitAction();
}

void GitSubmitEditorWidget::commitOnlySlot()
{
    m_pushAction = NoPush;
    updateSubmitAction();
}

void GitSubmitEditorWidget::commitAndPushSlot()
{
    m_pushAction = NormalPush;
    updateSubmitAction();
}

void GitSubmitEditorWidget::commitAndPushToGerritSlot()
{
    m_pushAction = PushToGerrit;
    updateSubmitAction();
}

bool GitSubmitEditorWidget::emailIsValid() const
{
    int pos = m_gitSubmitPanel->emailLineEdit->cursorPosition();
    QString text = m_gitSubmitPanel->emailLineEdit->text();
    return m_emailValidator->validate(text, pos) == QValidator::Acceptable;
}

} // namespace Internal
} // namespace Git
