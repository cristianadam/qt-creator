#include <extensionsystem/iplugin.h>

    m_client.showDiffEditor(state.currentFileTopLevel(), {state.relativeCurrentFile()});
    m_client.log(state.currentFileTopLevel(), {state.relativeCurrentFile()}, {}, true);
    m_client.showDiffEditor(state.topLevel());
    m_client.showDiffEditor(m_submitRepository, files);
class MercurialTest final : public QObject
{
    Q_OBJECT

private slots:
    void testDiffFileResolving_data();
    void testDiffFileResolving();
    void testLogResolving();
};

void MercurialTest::testDiffFileResolving_data()
void MercurialTest::testDiffFileResolving()
void MercurialTest::testLogResolving()
class MercurialPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Mercurial.json")

    ~MercurialPlugin() final
    {
        delete dd;
        dd = nullptr;
    }

    void initialize() final
    {
        dd = new MercurialPluginPrivate;
#ifdef WITH_TESTS
        addTest<MercurialTest>();
#endif
    }

    void extensionsInitialized() final
    {
        dd->extensionsInitialized();
    }
};


#include "mercurialplugin.moc"