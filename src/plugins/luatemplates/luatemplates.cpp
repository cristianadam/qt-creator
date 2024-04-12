// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <lua/bindings/layout.h>
#include <lua/luaengine.h>

#include <coreplugin/dialogs/promptoverwritedialog.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/iwizardfactory.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/algorithm.h>
#include <utils/expected.h>
#include <utils/layoutbuilder.h>
#include <utils/mimeutils.h>
#include <utils/wizard.h>
#include <utils/wizardpage.h>

#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorertr.h>
#include <projectexplorer/projectwizardpage.h>

#include <texteditor/icodestylepreferences.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/storagesettings.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/textindenter.h>

#include <QMessageBox>

using namespace Utils;
using namespace Layouting;
using namespace Core;
using namespace ProjectExplorer;
using namespace TextEditor;

namespace LuaTemplates {

static ICodeStylePreferences *codeStylePreferences(Project *project, Id languageId)
{
    if (!languageId.isValid())
        return nullptr;

    if (project)
        return project->editorConfiguration()->codeStyle(languageId);

    return TextEditorSettings::codeStyle(languageId);
}

class LuaWizard : public Wizard
{
public:
    expected_str<bool> promptForOverwrite(GeneratedFiles files)
    {
        FilePaths existingFiles;
        bool oddStuffFound = false;

        for (const auto &f : files) {
            if (f.filePath().exists() && !(f.attributes() & GeneratedFile::ForceOverwrite)
                && !(f.attributes() & GeneratedFile::KeepExistingFileAttribute))
                existingFiles.append(f.filePath());
        }
        if (existingFiles.isEmpty())
            return true;

        // Before prompting to overwrite existing files, loop over files and check
        // if there is anything blocking overwriting them (like them being links or folders).
        // Format a file list message as ( "<file1> [readonly], <file2> [folder]").
        const QString commonExistingPath = FileUtils::commonPath(existingFiles).toUserOutput();
        const int commonPathSize = commonExistingPath.size();
        QString fileNamesMsgPart;
        for (const FilePath &filePath : std::as_const(existingFiles)) {
            if (filePath.exists()) {
                if (!fileNamesMsgPart.isEmpty())
                    fileNamesMsgPart += QLatin1String(", ");
                const QString namePart = filePath.toUserOutput().mid(commonPathSize);
                if (filePath.isDir()) {
                    oddStuffFound = true;
                    fileNamesMsgPart += Tr::tr("%1 [folder]").arg(namePart);
                } else if (filePath.isSymLink()) {
                    oddStuffFound = true;
                    fileNamesMsgPart += Tr::tr("%1 [symbolic link]").arg(namePart);
                } else if (!filePath.isWritableDir() && !filePath.isWritableFile()) {
                    oddStuffFound = true;
                    fileNamesMsgPart += Tr::tr("%1 [read only]").arg(namePart);
                }
            }
        }

        if (oddStuffFound) {
            return make_unexpected(
                Tr::tr("The directory %1 contains files which cannot be overwritten:\n%2.")
                    .arg(commonExistingPath)
                    .arg(fileNamesMsgPart));
        }

        // Prompt to overwrite existing files.
        PromptOverwriteDialog overwriteDialog;

        // Scripts cannot handle overwrite
        overwriteDialog.setFiles(existingFiles);
        for (const auto &file : files) {
            if (!allowKeepingExistingFiles)
                overwriteDialog.setFileEnabled(file.filePath(), false);
        }
        if (overwriteDialog.exec() != QDialog::Accepted)
            return false;

        const QSet<FilePath> existingFilesToKeep = Utils::toSet(overwriteDialog.uncheckedFiles());
        if (existingFilesToKeep.size() == files.size()) // All exist & all unchecked->Cancel.
            return false;

        // Set 'keep' attribute in files
        for (auto &file : files) {
            if (!existingFilesToKeep.contains(file.filePath()))
                continue;

            file.setAttributes(file.attributes() | GeneratedFile::KeepExistingFileAttribute);
        }
        return true;
    }

    void formatFile(GeneratedFile &file)
    {
        if (file.isBinary() || file.contents().isEmpty())
            return; // nothing to do

        Id languageId = TextEditorSettings::languageId(
            Utils::mimeTypeForFile(file.filePath()).name());

        if (!languageId.isValid())
            return; // don't modify files like *.ui, *.pro

        // TODO:
        auto baseProject
            = nullptr; // qobject_cast<Project *>( wizard->property("SelectedProject").value<QObject *>());

        ICodeStylePreferencesFactory *factory = TextEditorSettings::codeStyleFactory(languageId);

        QTextDocument doc(file.contents());
        QTextCursor cursor(&doc);
        Indenter *indenter = nullptr;
        if (factory) {
            indenter = factory->createIndenter(&doc);
            indenter->setFileName(file.filePath());
        }
        if (!indenter)
            indenter = new TextIndenter(&doc);
        ICodeStylePreferences *codeStylePrefs = codeStylePreferences(baseProject, languageId);
        indenter->setCodeStylePreferences(codeStylePrefs);

        cursor.select(QTextCursor::Document);
        indenter->indent(cursor, QChar::Null, codeStylePrefs->currentTabSettings());
        delete indenter;
        if (TextEditor::globalStorageSettings().m_cleanWhitespace) {
            QTextBlock block = doc.firstBlock();
            while (block.isValid()) {
                TabSettings::removeTrailingWhitespace(cursor, block);
                block = block.next();
            }
        }
        file.setContents(doc.toPlainText());
    }

    void formatFiles(GeneratedFiles &files)
    {
        for (auto &file : files)
            formatFile(file);
    }

    void accept() override
    {
        auto files = Lua::LuaEngine::safe_call<GeneratedFiles>(fileFactory);
        QTC_ASSERT_EXPECTED(files, return);

        auto result = promptForOverwrite(*files);
        if (!result) {
            QMessageBox::warning(this, Tr::tr("Failed to Overwrite Files"), result.error());
            return;
        }

        formatFiles(*files);

        if (*result) {
            for (const auto &file : *files) {
                QString errorMsg;
                if (file.attributes().testFlag(GeneratedFile::KeepExistingFileAttribute))
                    continue;

                if (!file.write(&errorMsg)) {
                    qWarning() << "Failed writing file:" << errorMsg;
                    continue;
                } else if (file.attributes().testFlag(GeneratedFile::OpenEditorAttribute)) {
                    EditorManager::openEditor(file.filePath());
                }
            }
        }

        Wizard::accept();
        return;
    }

    void reject() override { Wizard::reject(); }

    sol::protected_function fileFactory;
    bool allowKeepingExistingFiles{true};
};

class WizardFactory : public IWizardFactory
{
public:
    WizardFactory() {}

    Wizard *runWizardImpl(
        const FilePath &path,
        QWidget * /*parent*/,
        Id /*platform*/,
        const QVariantMap & /*variables*/,
        bool showWizard = true) override
    {
        auto wizard = Lua::LuaEngine::safe_call<LuaWizard *>(m_setupFunction, path);
        QTC_ASSERT_EXPECTED(wizard, return nullptr);

        if (showWizard)
            (*wizard)->show();
        return (*wizard);
    }

    sol::protected_function m_setupFunction;
};

class LuaWizardPage : public WizardPage
{
public:
    void initializePage() override
    {
        if (m_initializePage) {
            auto res = Lua::LuaEngine::void_safe_call(*m_initializePage, this);
            QTC_CHECK_EXPECTED(res);
        }
        WizardPage::initializePage();
    }
    std::optional<sol::function> m_initializePage;
};

class SummaryPage : public ProjectWizardPage
{
public:
    void initializePage() override
    {
        if (m_initializePage) {
            auto res = Lua::LuaEngine::void_safe_call(*m_initializePage, this);
            QTC_CHECK_EXPECTED(res);
        }

        FilePaths paths = Utils::transform(m_files,
                                           [](const GeneratedFile &f) { return f.filePath(); });
        initializeProjectTree(nullptr,
                              paths,
                              IWizardFactory::WizardKind::FileWizard,
                              ProjectAction::AddNewFile);

        initializeVersionControls();

        ProjectWizardPage::initializePage();
    }

    void setFiles(GeneratedFiles files)
    {
        m_files = std::move(files);
        FilePaths paths = Utils::transform(m_files,
                                           [](const GeneratedFile &f) { return f.filePath(); });
        ProjectWizardPage::setFiles(paths);
    }

    GeneratedFiles m_files;
    std::optional<sol::protected_function> m_initializePage;
};

class LuaTemplatesPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "LuaTemplates.json")

public:
    LuaTemplatesPlugin() {}

private:
    void initialize() final
    {
        Lua::LuaEngine::registerProvider("Wizard", [](sol::state_view lua) -> sol::object {
            sol::table wizard = lua.create_table();
            wizard.new_usertype<WizardFactory>("Factory", sol::no_constructor);

            wizard.new_usertype<SummaryPage>("SummaryPage",
                                             "setFiles",
                                             [](SummaryPage *page, QList<GeneratedFile> files) {
                                                 page->setFiles(std::move(files));
                                             });

            wizard.new_usertype<LuaWizard>(
                "Wizard",
                sol::no_constructor,
                "addPage",
                [](LuaWizard *wizard, sol::table options) {
                    LuaWizardPage *page = new LuaWizardPage();

                    page->m_initializePage = options.get<std::optional<sol::function>>(
                        "initializePage");

                    page->setTitle(options.get<QString>("title"));
                    Layouting::LayoutItem *item = Lua::fromLua(options.get<sol::object>("layout"));
                    item->attachTo(page);
                    wizard->addPage(page);
                    return page;
                },
                "addSummaryPage",
                [](LuaWizard *wizard, sol::table options) {
                    SummaryPage *page = new SummaryPage();

                    page->m_initializePage = options.get<std::optional<sol::function>>(
                        "initializePage");

                    wizard->addPage(page);
                    return page;
                });

            wizard.set_function("create", [](sol::table options) {
                std::unique_ptr<LuaWizard> wizard(new LuaWizard());
                wizard->fileFactory = options.get<sol::function>("fileFactory");
                wizard->allowKeepingExistingFiles
                    = options.get<std::optional<bool>>("allowKeepingExistingFiles").value_or(true);
                return wizard.release();
            });

            wizard.set_function("registerFactory", [](sol::table options) {
                std::unique_ptr<WizardFactory> factory(new WizardFactory());

                factory->setId(Utils::Id::fromString(options.get<QString>("id")));
                factory->setDisplayName(options.get<QString>("displayName"));
                factory->setDescription(options.get<QString>("description"));
                factory->setCategory(options.get<QString>("category"));
                factory->setDisplayCategory(options.get<QString>("displayCategory"));
                factory->setFlags(IWizardFactory::PlatformIndependent);
                factory->setIcon(QIcon(options.get_or<QString>("icon", {})),
                                 options.get_or<QString>("iconText", {}));
                factory->m_setupFunction = options.get<sol::function>("factory");

                const QString icon = options.get_or<QString>("icon", {});
                if (!icon.isEmpty())
                    factory->setIcon(QIcon(icon));

                IWizardFactory::registerFactory(factory.get());

                return factory.release();
            });

            return wizard;
        });
    }
};

} // namespace LuaTemplates

#include "luatemplates.moc"
