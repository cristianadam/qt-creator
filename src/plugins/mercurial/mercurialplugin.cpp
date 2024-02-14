#include "mercurialplugin.h"

#include <utils/parameteraction.h>
#include <vcsbase/basevcseditorfactory.h>
#include <vcsbase/basevcssubmiteditorfactory.h>
class MercurialTopicCache : public Core::IVersionControl::TopicCache
{
public:
    MercurialTopicCache(MercurialClient *client) : m_client(client) {}

protected:
    FilePath trackFile(const FilePath &repository) override
    {
        return repository.pathAppended(".hg/branch");
    }

    QString refreshTopic(const FilePath &repository) override
    {
        return m_client->branchQuerySync(repository.toString());
    }

private:
    MercurialClient *m_client;
};

const VcsBaseEditorParameters logEditorParameters {
    LogOutput,
    Constants::FILELOG_ID,
    Constants::FILELOG_DISPLAY_NAME,
    Constants::LOGAPP
};

const VcsBaseEditorParameters annotateEditorParameters {
    AnnotateOutput,
    Constants::ANNOTATELOG_ID,
    Constants::ANNOTATELOG_DISPLAY_NAME,
    Constants::ANNOTATEAPP
};

const VcsBaseEditorParameters diffEditorParameters {
    DiffOutput,
    Constants::DIFFLOG_ID,
    Constants::DIFFLOG_DISPLAY_NAME,
    Constants::DIFFAPP
};

const VcsBaseSubmitEditorParameters submitEditorParameters {
    Constants::COMMITMIMETYPE,
    Constants::COMMIT_ID,
    Constants::COMMIT_DISPLAY_NAME,
    VcsBaseSubmitEditorParameters::DiffFiles
};

class MercurialPluginPrivate final : public VcsBase::VcsBasePluginPrivate
    void vcsDescribe(const FilePath &source, const QString &id) final { m_client.view(source, id); }
    void updateActions(VcsBase::VcsBasePluginPrivate::ActionState) final;
    MercurialClient m_client;

    ParameterAction *m_addAction = nullptr;
    ParameterAction *m_deleteAction = nullptr;
    ParameterAction *annotateFile = nullptr;
    ParameterAction *diffFile = nullptr;
    ParameterAction *logFile = nullptr;
    ParameterAction *revertFile = nullptr;
    ParameterAction *statusFile = nullptr;
    VcsSubmitEditorFactory submitEditorFactory {
        submitEditorParameters,
        [] { return new CommitEditor; },
        this
    };

    VcsEditorFactory logEditorFactory {
        &logEditorParameters,
        [this] { return new MercurialEditorWidget(&m_client); },
    };

    VcsEditorFactory annotateEditorFactory {
        &annotateEditorParameters,
        [this] { return new MercurialEditorWidget(&m_client); },
    };

    VcsEditorFactory diffEditorFactory {
        &diffEditorParameters,
        [this] { return new MercurialEditorWidget(&m_client); },
    };
MercurialPlugin::~MercurialPlugin()
{
    delete dd;
    dd = nullptr;
}

void MercurialPlugin::initialize()
{
    dd = new MercurialPluginPrivate;
}

void MercurialPlugin::extensionsInitialized()
{
    dd->extensionsInitialized();
}

    : VcsBase::VcsBasePluginPrivate(Core::Context(Constants::MERCURIAL_CONTEXT))
    setTopicCache(new MercurialTopicCache(&m_client));
    connect(&m_client, &VcsBaseClient::changed, this, &MercurialPluginPrivate::changed);
    connect(&m_client, &MercurialClient::needUpdate, this, &MercurialPluginPrivate::update);
    annotateFile = new ParameterAction(Tr::tr("Annotate Current File"), Tr::tr("Annotate \"%1\""), ParameterAction::EnabledWithParameter, this);
    diffFile = new ParameterAction(Tr::tr("Diff Current File"), Tr::tr("Diff \"%1\""), ParameterAction::EnabledWithParameter, this);
    logFile = new ParameterAction(Tr::tr("Log Current File"), Tr::tr("Log \"%1\""), ParameterAction::EnabledWithParameter, this);
    statusFile = new ParameterAction(Tr::tr("Status Current File"), Tr::tr("Status \"%1\""), ParameterAction::EnabledWithParameter, this);
    m_addAction = new ParameterAction(Tr::tr("Add"), Tr::tr("Add \"%1\""), ParameterAction::EnabledWithParameter, this);
    m_deleteAction = new ParameterAction(Tr::tr("Delete..."), Tr::tr("Delete \"%1\"..."), ParameterAction::EnabledWithParameter, this);
    revertFile = new ParameterAction(Tr::tr("Revert Current File..."), Tr::tr("Revert \"%1\"..."), ParameterAction::EnabledWithParameter, this);
    m_client.synchronousAdd(state.currentFileTopLevel(), state.relativeCurrentFile());
    m_client.annotate(state.currentFileTopLevel(), state.relativeCurrentFile(), currentLine);
    m_client.diff(state.currentFileTopLevel(), QStringList(state.relativeCurrentFile()));
    m_client.log(state.currentFileTopLevel(), QStringList(state.relativeCurrentFile()), {}, true);
    m_client.revertFile(state.currentFileTopLevel(), state.relativeCurrentFile(), reverter.revision());
    m_client.status(state.currentFileTopLevel(), state.relativeCurrentFile());
    m_client.diff(state.topLevel());
    m_client.log(state.topLevel());
    m_client.revertAll(state.topLevel(), reverter.revision());
    m_client.status(state.topLevel());
    m_client.synchronousPull(dialog.workingDir(), dialog.getRepositoryString());
    m_client.synchronousPush(dialog.workingDir(), dialog.getRepositoryString());
    m_client.update(state.topLevel(), updateDialog.revision());
    m_client.import(state.topLevel(), fileNames);
    m_client.incoming(state.topLevel(), dialog.getRepositoryString());
    m_client.outgoing(state.topLevel());
    connect(&m_client, &MercurialClient::parsedStatus, this, &MercurialPluginPrivate::showCommitWidget);
    m_client.emitParsedStatus(m_submitRepository);
    disconnect(&m_client, &MercurialClient::parsedStatus, this, &MercurialPluginPrivate::showCommitWidget);
    m_client.diff(m_submitRepository, files);
        m_client.commit(m_submitRepository, files, editorFile->filePath().toString(),
                        extraOptions);
void MercurialPluginPrivate::updateActions(VcsBasePluginPrivate::ActionState as)
    return m_client.isVcsDirectory(filePath);
    const FilePath topLevelFound = m_client.findTopLevelForFile(filePath);
    return m_client.managesFile(workingDirectory, fileName);
    return m_client.synchronousAdd(filePath.parentDir(), filePath.fileName());
    return m_client.synchronousRemove(filePath.parentDir(), filePath.fileName());
    return m_client.synchronousMove(from.parentDir(),
    return m_client.synchronousCreateRepository(directory);
    m_client.annotate(filePath.parentDir(), filePath.fileName(), line);
    auto command = VcsBaseClient::createVcsCommand(baseDirectory, m_client.processEnvironment());
    return m_client.manifestSync(topLevel, topLevelDir.relativeFilePath(filename));
void MercurialPlugin::testDiffFileResolving_data()
void MercurialPlugin::testDiffFileResolving()
void MercurialPlugin::testLogResolving()