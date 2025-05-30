// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "scxmldocument.h"
#include "scxmleditortr.h"
#include "scxmlnamespace.h"
#include "scxmltagutils.h"
#include "undocommands.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

using namespace ScxmlEditor::PluginInterface;

ScxmlDocument::ScxmlDocument(const QString &fileName, QObject *parent)
    : QObject(parent)
{
    initVariables();
    m_fileName = fileName;
    load(fileName);
}

ScxmlDocument::ScxmlDocument(const QByteArray &data, QObject *parent)
    : QObject(parent)
{
    initVariables();
    load(QLatin1String(data));
}

ScxmlDocument::~ScxmlDocument()
{
    clear(false);
}

void ScxmlDocument::initVariables()
{
    m_idDelimiter = "::";
    m_undoStack = new QUndoStack(this);
    connect(m_undoStack, &QUndoStack::cleanChanged, this, &ScxmlDocument::documentChanged);
}

void ScxmlDocument::clear(bool createRoot)
{
    m_currentTag = nullptr;
    m_nextIdHash.clear();

    // First clear undostack
    m_undoStack->clear();

    // Second delete all other available tags
    // tags will call the removeChild-function -> m_tags will be cleared
    for (int i = m_tags.count(); i--;)
        delete m_tags[i];

    m_rootTags.clear();
    clearNamespaces();

    if (createRoot) {
        pushRootTag(createScxmlTag());
        rootTag()->setAttribute("qt:editorversion", QCoreApplication::applicationVersion());

        auto ns = new ScxmlNamespace("qt", "http://www.qt.io/2015/02/scxml-ext");
        ns->setTagVisibility("editorInfo", false);
        addNamespace(ns);
    }

    m_useFullNameSpace = false;
}

QString ScxmlDocument::nameSpaceDelimiter() const
{
    return m_idDelimiter;
}

bool ScxmlDocument::useFullNameSpace() const
{
    return m_useFullNameSpace;
}

void ScxmlDocument::setNameSpaceDelimiter(const QString &delimiter)
{
    m_idDelimiter = delimiter;
}

QString ScxmlDocument::fileName() const
{
    return m_fileName;
}

void ScxmlDocument::setFileName(const QString &filename)
{
    m_fileName = filename;
}

ScxmlNamespace *ScxmlDocument::scxmlNamespace(const QString &prefix)
{
    return m_namespaces.value(prefix, 0);
}

void ScxmlDocument::addNamespace(ScxmlNamespace *ns)
{
    if (!ns)
        return;

    delete m_namespaces.take(ns->prefix());
    m_namespaces[ns->prefix()] = ns;

    ScxmlTag *scxmlTag = scxmlRootTag();
    if (scxmlTag) {
        for (ScxmlNamespace *ns : std::as_const(m_namespaces)) {
            QString prefix = ns->prefix();
            if (prefix.isEmpty())
                prefix = "xmlns";

            if (prefix.startsWith("xmlns"))
                scxmlTag->setAttribute(prefix, ns->name());
            else
                scxmlTag->setAttribute(QString::fromLatin1("xmlns:%1").arg(prefix), ns->name());
        }
    }
}

void ScxmlDocument::clearNamespaces()
{
    while (!m_namespaces.isEmpty()) {
        delete m_namespaces.take(m_namespaces.firstKey());
    }
}

bool ScxmlDocument::generateSCXML(QIODevice *io, ScxmlTag *tag) const
{
    QXmlStreamWriter xml(io);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    if (tag)
        tag->writeXml(xml);
    else
        rootTag()->writeXml(xml);
    xml.writeEndDocument();

    return !xml.hasError();
}

ScxmlTag *ScxmlDocument::createScxmlTag()
{
    auto tag = new ScxmlTag(Scxml, this);
    for (ScxmlNamespace *ns : std::as_const(m_namespaces)) {
        QString prefix = ns->prefix();
        if (prefix.isEmpty())
            prefix = "xmlns";

        if (prefix.startsWith("xmlns"))
            tag->setAttribute(prefix, ns->name());
        else
            tag->setAttribute(QString::fromLatin1("xmlns:%1").arg(prefix), ns->name());
    }
    return tag;
}

bool ScxmlDocument::hasLayouted() const
{
    return m_hasLayouted;
}
QColor ScxmlDocument::getColor(int depth) const
{
    return m_colors.isEmpty() ? QColor(Qt::gray) : m_colors[depth % m_colors.count()];
}

void ScxmlDocument::setLevelColors(const QList<QColor> &colors)
{
    m_colors = colors;
    emit colorThemeChanged();
}

bool ScxmlDocument::load(QIODevice *io)
{
    m_currentTag = nullptr;
    clearNamespaces();

    bool ok = true;
    clear(false);

    QXmlStreamReader xml(io);
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartDocument)
            continue;

        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("scxml")) {
                // Get and add namespaces
                QXmlStreamNamespaceDeclarations ns = xml.namespaceDeclarations();
                for (int i = 0; i < ns.count(); ++i)
                    addNamespace(new ScxmlNamespace(ns[i].prefix().toString(), ns[i].namespaceUri().toString()));

                // create root tag
                pushRootTag(createScxmlTag());

                // and read other tags also
                rootTag()->readXml(xml);

                // Check editorversion
                m_hasLayouted = rootTag()->hasAttribute("qt:editorversion");
                rootTag()->setAttribute("qt:editorversion", QCoreApplication::applicationVersion());
            }
        }

        if (token == QXmlStreamReader::Invalid)
            break;
    }

    if (xml.hasError()) {
        m_hasError = true;
        initErrorMessage(xml, io);
        m_fileName.clear();
        ok = false;

        clear();
    } else {
        m_hasError = false;
        m_lastError.clear();
    }

    m_undoStack->setClean();

    return ok;
}

void ScxmlDocument::initErrorMessage(const QXmlStreamReader &xml, QIODevice *io)
{
    QString errorString;
    switch (xml.error()) {
    case QXmlStreamReader::Error::UnexpectedElementError:
        errorString = Tr::tr("Unexpected element.");
        break;
    case QXmlStreamReader::Error::NotWellFormedError:
        errorString = Tr::tr("Not well formed.");
        break;
    case QXmlStreamReader::Error::PrematureEndOfDocumentError:
        errorString = Tr::tr("Premature end of document.");
        break;
    case QXmlStreamReader::Error::CustomError:
        errorString = Tr::tr("Custom error.");
        break;
    default:
        break;
    }

    QString lineString;
    io->seek(0);
    for (int i = 0; i < xml.lineNumber() - 1; ++i)
        io->readLine();
    lineString = QLatin1String(io->readLine());

    m_lastError = Tr::tr("Error in reading XML.\nType: %1 (%2)\nDescription: %3\n\nRow: %4, Column: %5\n%6")
                      .arg(xml.error())
                      .arg(errorString)
                      .arg(xml.errorString())
                      .arg(xml.lineNumber())
                      .arg(xml.columnNumber())
                      .arg(lineString);
}

bool ScxmlDocument::pasteData(const QByteArray &data, const QPointF &minPos, const QPointF &pastePos)
{
    if (!m_currentTag)
        m_currentTag = rootTag();

    if (!m_currentTag) {
        m_hasError = true;
        m_lastError = Tr::tr("Current tag is not selected.");
        return false;
    }

    if (data.trimmed().isEmpty()) {
        m_hasError = true;
        m_lastError = Tr::tr("Pasted data is empty.");
        return false;
    }

    bool ok = true;
    m_undoStack->beginMacro(Tr::tr("Paste items"));

    QByteArray d(data);
    QBuffer buffer(&d);
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader xml(&buffer);
    for (ScxmlNamespace *ns : std::as_const(m_namespaces)) {
        xml.addExtraNamespaceDeclaration(QXmlStreamNamespaceDeclaration(ns->prefix(), ns->name()));
    }

    m_idMap.clear();
    QList<ScxmlTag*> addedTags;

    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartDocument)
            continue;

        if (token == QXmlStreamReader::StartElement) {
            if (xml.name().toString() == "scxml")
                continue;

            ScxmlTag *childTag = nullptr;
            if ((m_currentTag->tagType() == Initial || m_currentTag->tagType() == History) && xml.name().toString() == "transition")
                childTag = new ScxmlTag(InitialTransition, this);
            else
                childTag = new ScxmlTag(xml.prefix().toString(), xml.name().toString(), this);

            childTag->readXml(xml, true);
            addedTags << childTag;
        }

        if (token == QXmlStreamReader::Invalid)
            break;
    }

    if (xml.error()) {
        m_hasError = true;
        qDeleteAll(addedTags);
        addedTags.clear();
        initErrorMessage(xml, &buffer);
        ok = false;
    } else {
        m_hasError = false;
        m_lastError.clear();

        // Fine-tune names and coordinates
        for (int i = 0; i < addedTags.count(); ++i)
            TagUtils::modifyPosition(addedTags[i], minPos, pastePos);

        // Update targets and initial-attributes
        for (int i = 0; i < addedTags.count(); ++i)
            addedTags[i]->finalizeTagNames();

        // Add tags to the document
        addTags(m_currentTag, addedTags);
    }

    m_undoStack->endMacro();

    return ok;
}

void ScxmlDocument::load(const QString &fileName)
{
    if (QFileInfo::exists(fileName)) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (load(&file)) {
                m_fileName = fileName;
            }
        }
    }

    // If loading doesn't work, create root tag here
    if (m_rootTags.isEmpty()) {
        pushRootTag(createScxmlTag());
        rootTag()->setAttribute("qt:editorversion", QCoreApplication::applicationVersion());
    }

    auto ns = new ScxmlNamespace("qt", "http://www.qt.io/2015/02/scxml-ext");
    ns->setTagVisibility("editorInfo", false);
    addNamespace(ns);
}

void ScxmlDocument::printSCXML()
{
    qDebug() << content();
}

QByteArray ScxmlDocument::content(const QList<ScxmlTag*> &tags) const
{
    QByteArray result;
    if (!tags.isEmpty()) {
        QBuffer buffer(&result);
        buffer.open(QIODevice::WriteOnly);

        bool writeScxml = tags.count() > 1 || tags[0]->tagType() != Scxml;

        QXmlStreamWriter xml(&buffer);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        if (writeScxml)
            xml.writeStartElement("scxml");

        for (ScxmlTag *tag : tags) {
            tag->writeXml(xml);
        }
        xml.writeEndDocument();

        if (writeScxml)
            xml.writeEndElement();
    }
    return result;
}

QByteArray ScxmlDocument::content(ScxmlTag *tag) const
{
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    generateSCXML(&buffer, tag);
    return byteArray;
}

bool ScxmlDocument::save()
{
    return save(m_fileName);
}

bool ScxmlDocument::save(const QString &fileName)
{
    QString name(fileName);
    if (!name.endsWith(".scxml", Qt::CaseInsensitive))
        name.append(".scxml");

    bool ok = true;

    QFile file(name);
    if (file.open(QIODevice::WriteOnly)) {
        ok = generateSCXML(&file, scxmlRootTag());
        if (ok) {
            m_fileName = name;
            m_undoStack->setClean();
        }
        file.close();
        if (!ok)
            m_lastError = Tr::tr("Cannot save XML to the file %1.").arg(fileName);
    } else {
        ok = false;
        m_lastError = Tr::tr("Cannot open file %1.").arg(fileName);
    }

    return ok;
}

void ScxmlDocument::setContent(ScxmlTag *tag, const QString &content)
{
    if (tag && !m_undoRedoRunning)
        m_undoStack->push(new SetContentCommand(this, tag, content));
}

void ScxmlDocument::setValue(ScxmlTag *tag, const QString &key, const QString &value)
{
    if (tag && !m_undoRedoRunning)
        m_undoStack->push(new SetAttributeCommand(this, tag, key, value));
}

void ScxmlDocument::setValue(ScxmlTag *tag, int attributeIndex, const QString &value)
{
    if (tag && attributeIndex >= 0 && attributeIndex < tag->info()->n_attributes)
        m_undoStack->push(new SetAttributeCommand(this, tag, QLatin1String(tag->info()->attributes[attributeIndex].name), value));
}

void ScxmlDocument::setEditorInfo(ScxmlTag *tag, const QString &key, const QString &value)
{
    if (tag && !m_undoRedoRunning)
        m_undoStack->push(new SetEditorInfoCommand(this, tag, key, value));
}

void ScxmlDocument::setCurrentTag(ScxmlTag *tag)
{
    if (tag != m_currentTag) {
        emit beginTagChange(TagCurrentChanged, tag, QVariant());
        m_currentTag = tag;
        emit endTagChange(TagCurrentChanged, tag, QVariant());
    }
}

ScxmlTag *ScxmlDocument::currentTag() const
{
    return m_currentTag;
}

void ScxmlDocument::changeParent(ScxmlTag *child, ScxmlTag *newParent, int tagIndex)
{
    if (child && child->parentTag() != newParent && !m_undoRedoRunning)
        m_undoStack->push(new ChangeParentCommand(this, child, !newParent ? rootTag() : newParent, tagIndex));
}

void ScxmlDocument::changeOrder(ScxmlTag *child, int newPos)
{
    if (child && !m_undoRedoRunning) {
        ScxmlTag *parentTag = child->parentTag();
        if (parentTag) {
            m_undoStack->push(new ChangeOrderCommand(this, child, parentTag, newPos));
        }
    }
}

void ScxmlDocument::addTags(ScxmlTag *parent, const QList<ScxmlTag*> tags)
{
    if (m_undoRedoRunning)
        return;

    if (!parent)
        parent = rootTag();

    m_undoStack->push(new AddRemoveTagsBeginCommand(this, parent));
    for (int i = 0; i < tags.count(); ++i)
        addTag(parent, tags[i]);
    m_undoStack->push(new AddRemoveTagsEndCommand(this, parent));
}

void ScxmlDocument::addTag(ScxmlTag *parent, ScxmlTag *child)
{
    if (m_undoRedoRunning)
        return;

    if (!parent)
        parent = rootTag();

    if (parent && child) {
        m_undoStack->beginMacro(Tr::tr("Add Tag"));
        addTagRecursive(parent, child);
        m_undoStack->endMacro();
    }
}

void ScxmlDocument::removeTag(ScxmlTag *tag)
{
    if (tag && !m_undoRedoRunning) {
        // Create undo/redo -macro, because state can includes lot of child-states
        m_undoStack->beginMacro(Tr::tr("Remove Tag"));
        removeTagRecursive(tag);
        m_undoStack->endMacro();
    }
}

void ScxmlDocument::addTagRecursive(ScxmlTag *parent, ScxmlTag *tag)
{
    if (tag && !m_undoRedoRunning) {
        m_undoStack->push(new AddRemoveTagCommand(this, parent, tag, TagAddChild));

        // First create AddRemoveTagCommands for the all children recursive
        for (int i = 0; i < tag->childCount(); ++i)
            addTagRecursive(tag, tag->child(i));
    }
}

void ScxmlDocument::removeTagRecursive(ScxmlTag *tag)
{
    if (tag && !m_undoRedoRunning) {
        // First create AddRemoveTagCommands for the all children recursive
        int childCount = tag->childCount();
        for (int i = childCount; i--;)
            removeTagRecursive(tag->child(i));

        m_undoStack->push(new AddRemoveTagCommand(this, tag->parentTag(), tag, TagRemoveChild));
    }
}

void ScxmlDocument::setUseFullNameSpace(bool use)
{
    if (m_useFullNameSpace != use)
        m_undoStack->push(new ChangeFullNameSpaceCommand(this, scxmlRootTag(), use));
}

QString ScxmlDocument::nextUniqueId(const QString &key)
{
    QString name;
    bool bFound = false;
    int id = 0;
    while (true) {
        id = m_nextIdHash.value(key, 0) + 1;
        m_nextIdHash[key] = id;
        bFound = false;
        name = QString::fromLatin1("%1_%2").arg(key).arg(id);

        // Check duplicate
        for (const ScxmlTag *tag : std::as_const(m_tags)) {
            if (tag->attribute("id") == name) {
                bFound = true;
                break;
            }
        }
        if (!bFound)
            break;
    }
    return name;
}

QString ScxmlDocument::getUniqueCopyId(const ScxmlTag *tag)
{
    const QString key = tag->attribute("id");
    QString name = key;
    int counter = 1;
    bool bFound = false;

    while (true) {
        bFound = false;
        // Check duplicate
        for (const ScxmlTag *t : std::as_const(m_tags)) {
            if (t->attribute("id") == name && t != tag) {
                name = QString::fromLatin1("%1_Copy%2").arg(key).arg(counter);
                bFound = true;
                counter++;
            }
        }

        if (!bFound)
            break;
    }

    return name;
}

bool ScxmlDocument::changed() const
{
    return !m_undoStack->isClean();
}

ScxmlTag *ScxmlDocument::scxmlRootTag() const
{
    ScxmlTag *tag = rootTag();
    while (tag && tag->tagType() != Scxml) {
        tag = tag->parentTag();
    }

    return tag;
}

ScxmlTag *ScxmlDocument::tagForId(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    ScxmlTag *root = scxmlRootTag();
    return root ? root->tagForId(id) : nullptr;
}

ScxmlTag *ScxmlDocument::rootTag() const
{
    return m_rootTags.isEmpty() ? 0 : m_rootTags.last();
}

void ScxmlDocument::pushRootTag(ScxmlTag *tag)
{
    m_rootTags << tag;
}

ScxmlTag *ScxmlDocument::popRootTag()
{
    return m_rootTags.takeLast();
}

void ScxmlDocument::deleteRootTags()
{
    while (!m_rootTags.isEmpty())
        delete m_rootTags.takeLast();
}

QUndoStack *ScxmlDocument::undoStack() const
{
    return m_undoStack;
}

void ScxmlDocument::addChild(ScxmlTag *tag)
{
    if (!m_tags.contains(tag))
        m_tags << tag;
}

void ScxmlDocument::removeChild(ScxmlTag *tag)
{
    m_tags.removeAll(tag);
}

void ScxmlDocument::setUndoRedoRunning(bool para)
{
    m_undoRedoRunning = para;
}

QFileInfo ScxmlDocument::qtBinDir() const
{
    return m_qtBinDir;
}
