// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "shortcutsettings.h"

#include "ioptionspage.h"
#include "../actionmanager/actionmanager.h"
#include "../actionmanager/command.h"
#include "../actionmanager/commandmappings.h"
#include "../coreconstants.h"
#include "../coreplugintr.h"
#include "../documentmanager.h"
#include "../icore.h"

#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <array>

using namespace Utils;

namespace Core::Internal {

const char kSeparator[] = " | ";

struct ShortcutItem final
{
    Command *m_cmd;
    QList<QKeySequence> m_keys;
    QTreeWidgetItem *m_item;
};

/*!
    \class Core::Internal::CommandsFile
    \internal
    \inmodule QtCreator
    \brief The CommandsFile class provides a collection of import and export commands.
*/

class CommandsFile final
{
public:
    CommandsFile(const FilePath &filePath) : m_filePath(filePath) {}

    QMap<QString, QList<QKeySequence> > importCommands() const;
    bool exportCommands(const QList<ShortcutItem *> &items);

private:
    const QString mappingElement = "mapping";
    const QString shortCutElement = "shortcut";
    const QString idAttribute = "id";
    const QString keyElement = "key";
    const QString valueAttribute = "value";

    FilePath m_filePath;
};


// XML attributes cannot contain these characters, and
// QXmlStreamWriter just bails out with an error.
// QKeySequence::toString() should probably not result in these
// characters, but it currently does, see QTCREATORBUG-29431
static bool containsInvalidCharacters(const QString &s)
{
    const auto end = s.constEnd();
    for (auto it = s.constBegin(); it != end; ++it) {
        // from QXmlStreamWriterPrivate::writeEscaped
        if (*it == u'\v' || *it == u'\f' || *it <= u'\x1F' || *it >= u'\uFFFE') {
            return true;
        }
    }
    return false;
}

static QString toAttribute(const QString &s)
{
    if (containsInvalidCharacters(s))
        return "0x" + QString::fromUtf8(s.toUtf8().toHex());
    return s;
}

static QString fromAttribute(const QStringView &s)
{
    if (s.startsWith(QLatin1String("0x")))
        return QString::fromUtf8(QByteArray::fromHex(s.sliced(2).toUtf8()));
    return s.toString();
}

/*!
    \internal
*/
QMap<QString, QList<QKeySequence>> CommandsFile::importCommands() const
{
    QMap<QString, QList<QKeySequence>> result;

    QFile file(m_filePath.toUrlishString());
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text))
        return result;

    QXmlStreamReader r(&file);

    QString currentId;

    while (!r.atEnd()) {
        switch (r.readNext()) {
        case QXmlStreamReader::StartElement: {
            const auto name = r.name();
            if (name == shortCutElement) {
                currentId = r.attributes().value(idAttribute).toString();
                if (!result.contains(currentId))
                    result.insert(currentId, {});
            } else if (name == keyElement) {
                QTC_ASSERT(!currentId.isEmpty(), continue);
                const QXmlStreamAttributes attributes = r.attributes();
                if (attributes.hasAttribute(valueAttribute)) {
                    QString keyString = fromAttribute(attributes.value(valueAttribute));
                    if (HostOsInfo::isMacHost())
                        keyString = keyString.replace("AlwaysCtrl", "Meta");
                    else
                        keyString = keyString.replace("AlwaysCtrl", "Ctrl");

                    QList<QKeySequence> keys = result.value(currentId);
                    result.insert(currentId, keys << QKeySequence(keyString));
                }
            } // if key element
        } // case QXmlStreamReader::StartElement
        default:
            break;
        } // switch
    } // while !atEnd
    file.close();
    return result;
}

/*!
    \internal
*/
bool CommandsFile::exportCommands(const QList<ShortcutItem *> &items)
{
    FileSaver saver(m_filePath, QIODevice::Text);
    if (!saver.hasError()) {
        QXmlStreamWriter w(saver.file());
        w.setAutoFormatting(true);
        w.setAutoFormattingIndent(1); // Historical, used to be QDom.
        w.writeStartDocument();
        w.writeDTD(QLatin1String("<!DOCTYPE KeyboardMappingScheme>"));
        w.writeComment(QString::fromLatin1(" Written by %1, %2. ").
                       arg(ICore::versionString(),
                           QDateTime::currentDateTime().toString(Qt::ISODate)));
        w.writeStartElement(mappingElement);
        for (const ShortcutItem *item : std::as_const(items)) {
            const Id id = item->m_cmd->id();
            if (item->m_keys.isEmpty() || item->m_keys.first().isEmpty()) {
                w.writeEmptyElement(shortCutElement);
                w.writeAttribute(idAttribute, id.toString());
            } else {
                w.writeStartElement(shortCutElement);
                w.writeAttribute(idAttribute, id.toString());
                for (const QKeySequence &k : item->m_keys) {
                    w.writeEmptyElement(keyElement);
                    w.writeAttribute(valueAttribute, toAttribute(k.toString()));
                }
                w.writeEndElement(); // Shortcut
            }
        }
        w.writeEndElement();
        w.writeEndDocument();

        if (!saver.setResult(&w))
            qWarning() << saver.errorString();
    }
    return saver.finalize().has_value();
}

static int translateModifiers(Qt::KeyboardModifiers state, const QString &text)
{
    int result = 0;
    // The shift modifier only counts when it is not used to type a symbol
    // that is only reachable using the shift key anyway
    if ((state & Qt::ShiftModifier) && (text.isEmpty()
                                        || !text.at(0).isPrint()
                                        || text.at(0).isLetterOrNumber()
                                        || text.at(0).isSpace()))
        result |= Qt::SHIFT;
    if (state & Qt::ControlModifier)
        result |= Qt::CTRL;
    if (state & Qt::MetaModifier)
        result |= Qt::META;
    if (state & Qt::AltModifier)
        result |= Qt::ALT;
    return result;
}

static QList<QKeySequence> cleanKeys(const QList<QKeySequence> &ks)
{
    return Utils::filtered(ks, [](const QKeySequence &k) { return !k.isEmpty(); });
}

static QString keySequenceToEditString(const QKeySequence &sequence)
{
    QString text = sequence.toString(QKeySequence::PortableText);
    if (Utils::HostOsInfo::isMacHost()) {
        // adapt the modifier names
        text.replace(QLatin1String("Ctrl"), QLatin1String("Cmd"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Alt"), QLatin1String("Opt"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Meta"), QLatin1String("Ctrl"), Qt::CaseInsensitive);
    }
    return text;
}

static QString keySequencesToEditString(const QList<QKeySequence> &sequence)
{
    return Utils::transform(cleanKeys(sequence), keySequenceToEditString).join(kSeparator);
}

static QString keySequencesToNativeString(const QList<QKeySequence> &sequence)
{
    return Utils::transform(cleanKeys(sequence),
                            [](const QKeySequence &k) {
                                return k.toString(QKeySequence::NativeText);
                            })
        .join(kSeparator);
}

static QKeySequence keySequenceFromEditString(const QString &editString)
{
    QString text = editString.trimmed();
    if (Utils::HostOsInfo::isMacHost()) {
        // adapt the modifier names
        text.replace(QLatin1String("Opt"), QLatin1String("Alt"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Ctrl"), QLatin1String("Meta"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Cmd"), QLatin1String("Ctrl"), Qt::CaseInsensitive);
    }
    return QKeySequence::fromString(text, QKeySequence::PortableText);
}

static bool keySequenceIsValid(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return false;
    for (int i = 0; i < sequence.count(); ++i) {
        if (sequence[i] == QKeyCombination(Qt::Key_unknown))
            return false;
    }
    return true;
}

static bool isTextKeySequence(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return false;
    const QKeyCombination keyCombination = sequence[0];
    if (keyCombination.keyboardModifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier))
        return false;
    return keyCombination.key() < Qt::Key_Escape;
}

static FilePath schemesPath()
{
    return Core::ICore::resourcePath("schemes");
}

static bool checkValidity(const QKeySequence &key, QString *warningMessage)
{
    if (key.isEmpty())
        return true;
    QTC_ASSERT(warningMessage, return true);
    if (!keySequenceIsValid(key)) {
        *warningMessage = Tr::tr("Invalid key sequence.");
        return false;
    }
    if (isTextKeySequence(key))
        *warningMessage = Tr::tr("Key sequence will not work in editor."); // FIXME: return false missing?
    return true;
}

class ShortcutButton final : public QPushButton
{
    Q_OBJECT

public:
    ShortcutButton(QWidget *parent = nullptr);

    QSize sizeHint() const final;

signals:
    void keySequenceChanged(const QKeySequence &sequence);

protected:
    bool eventFilter(QObject *obj, QEvent *evt) final;

private:
    void updateText();
    void handleToggleChange(bool toggleState);

    QString m_checkedText;
    QString m_uncheckedText;
    mutable int m_preferredWidth = -1;
    std::array<int, 4> m_key;
    int m_keyNum = 0;
};

ShortcutButton::ShortcutButton(QWidget *parent)
    : QPushButton(parent)
    , m_key({{0, 0, 0, 0}})
{
    setToolTip(::Core::Tr::tr("Click and type the new key sequence."));
    setCheckable(true);
    m_checkedText = ::Core::Tr::tr("Stop Recording");
    m_uncheckedText = ::Core::Tr::tr("Record");
    updateText();
    connect(this, &ShortcutButton::toggled, this, &ShortcutButton::handleToggleChange);
}

QSize ShortcutButton::sizeHint() const
{
    if (m_preferredWidth < 0) { // initialize size hint
        const QString originalText = text();
        auto that = const_cast<ShortcutButton *>(this);
        that->setText(m_checkedText);
        m_preferredWidth = QPushButton::sizeHint().width();
        that->setText(m_uncheckedText);
        int otherWidth = QPushButton::sizeHint().width();
        if (otherWidth > m_preferredWidth)
            m_preferredWidth = otherWidth;
        that->setText(originalText);
    }
    return QSize(m_preferredWidth, QPushButton::sizeHint().height());
}

bool ShortcutButton::eventFilter(QObject *obj, QEvent *evt)
{
    if (evt->type() == QEvent::ShortcutOverride) {
        evt->accept();
        return true;
    }
    if (evt->type() == QEvent::KeyRelease
               || evt->type() == QEvent::Shortcut
               || evt->type() == QEvent::Close/*Escape tries to close dialog*/) {
        return true;
    }
    if (evt->type() == QEvent::MouseButtonPress && isChecked()) {
        setChecked(false);
        return true;
    }
    if (evt->type() == QEvent::KeyPress) {
        auto k = static_cast<QKeyEvent*>(evt);
        int nextKey = k->key();
        if (m_keyNum > 3
                || nextKey == Qt::Key_Control
                || nextKey == Qt::Key_Shift
                || nextKey == Qt::Key_Meta
                || nextKey == Qt::Key_Alt) {
             return false;
        }

        nextKey |= translateModifiers(k->modifiers(), k->text());
        switch (m_keyNum) {
        case 0:
            m_key[0] = nextKey;
            break;
        case 1:
            m_key[1] = nextKey;
            break;
        case 2:
            m_key[2] = nextKey;
            break;
        case 3:
            m_key[3] = nextKey;
            break;
        default:
            break;
        }
        m_keyNum++;
        k->accept();
        emit keySequenceChanged(QKeySequence(m_key[0], m_key[1], m_key[2], m_key[3]));
        if (m_keyNum > 3)
            setChecked(false);
        return true;
    }
    return QPushButton::eventFilter(obj, evt);
}

void ShortcutButton::updateText()
{
    setText(isChecked() ? m_checkedText : m_uncheckedText);
}

void ShortcutButton::handleToggleChange(bool toogleState)
{
    updateText();
    m_keyNum = m_key[0] = m_key[1] = m_key[2] = m_key[3] = 0;
    if (toogleState) {
        if (QApplication::focusWidget())
            QApplication::focusWidget()->clearFocus(); // funny things happen otherwise
        qApp->installEventFilter(this);
    } else {
        qApp->removeEventFilter(this);
    }
}

class ShortcutInput final : public QObject
{
    Q_OBJECT

public:
    ShortcutInput();
    ~ShortcutInput();

    void addToLayout(QGridLayout *layout, int row);

    void setKeySequence(const QKeySequence &key);
    QKeySequence keySequence() const;

    using ConflictChecker = std::function<bool(QKeySequence)>;
    void setConflictChecker(const ConflictChecker &fun);

signals:
    void changed();
    void showConflictsRequested();

private:
    ConflictChecker m_conflictChecker;
    QPointer<QLabel> m_shortcutLabel;
    QPointer<Utils::FancyLineEdit> m_shortcutEdit;
    QPointer<ShortcutButton> m_shortcutButton;
    QPointer<QLabel> m_warningLabel;
};

ShortcutInput::ShortcutInput()
{
    m_shortcutLabel = new QLabel(Tr::tr("Key sequence:"));
    m_shortcutLabel->setToolTip(
        Utils::HostOsInfo::isMacHost()
            ? QLatin1String("<html><body>")
                  + Tr::tr("Use \"Cmd\", \"Opt\", \"Ctrl\", and \"Shift\" for modifier keys. "
                           "Use \"Escape\", \"Backspace\", \"Delete\", \"Insert\", \"Home\", and so "
                           "on, for special keys. "
                           "Combine individual keys with \"+\", "
                           "and combine multiple shortcuts to a shortcut sequence with \",\". "
                           "For example, if the user must hold the Ctrl and Shift modifier keys "
                           "while pressing Escape, and then release and press A, "
                           "enter \"Ctrl+Shift+Escape,A\".")
                  + QLatin1String("</body></html>")
            : QLatin1String("<html><body>")
                  + Tr::tr("Use \"Ctrl\", \"Alt\", \"Meta\", and \"Shift\" for modifier keys. "
                           "Use \"Escape\", \"Backspace\", \"Delete\", \"Insert\", \"Home\", and so "
                           "on, for special keys. "
                           "Combine individual keys with \"+\", "
                           "and combine multiple shortcuts to a shortcut sequence with \",\". "
                           "For example, if the user must hold the Ctrl and Shift modifier keys "
                           "while pressing Escape, and then release and press A, "
                           "enter \"Ctrl+Shift+Escape,A\".")
                  + QLatin1String("</body></html>"));

    m_shortcutEdit = new Utils::FancyLineEdit;
    m_shortcutEdit->setFiltering(true);
    m_shortcutEdit->setPlaceholderText(Tr::tr("Enter key sequence as text"));
    connect(m_shortcutEdit, &Utils::FancyLineEdit::textChanged, this, &ShortcutInput::changed);

    m_shortcutButton = new ShortcutButton;
    connect(m_shortcutButton,
            &ShortcutButton::keySequenceChanged,
            this,
            [this](const QKeySequence &k) { setKeySequence(k); });

    m_warningLabel = new QLabel;
    m_warningLabel->setTextFormat(Qt::RichText);
    QPalette palette = m_warningLabel->palette();
    palette.setColor(QPalette::Active,
                     QPalette::WindowText,
                     Utils::creatorColor(Utils::Theme::TextColorError));
    m_warningLabel->setPalette(palette);
    connect(m_warningLabel, &QLabel::linkActivated, this, &ShortcutInput::showConflictsRequested);

    m_shortcutEdit->setValidationFunction([this](const QString &) -> Result<> {
        QString warningMessage;
        const QKeySequence key = keySequenceFromEditString(m_shortcutEdit->text());
        const bool isValid = checkValidity(key, &warningMessage);
        m_warningLabel->setText(warningMessage);
        if (isValid && m_conflictChecker && m_conflictChecker(key)) {
            m_warningLabel->setText(Tr::tr(
                "Key sequence has potential conflicts. <a href=\"#conflicts\">Show.</a>"));
        }
        if (!isValid)
            return ResultError(warningMessage);
        return ResultOk;
    });
}

ShortcutInput::~ShortcutInput()
{
    delete m_shortcutLabel;
    delete m_shortcutEdit;
    delete m_shortcutButton;
    delete m_warningLabel;
}

void ShortcutInput::addToLayout(QGridLayout *layout, int row)
{
    layout->addWidget(m_shortcutLabel, row, 0);
    layout->addWidget(m_shortcutEdit, row, 1);
    layout->addWidget(m_shortcutButton, row, 2);

    layout->addWidget(m_warningLabel, row + 1, 0, 1, 2);
}

void ShortcutInput::setKeySequence(const QKeySequence &key)
{
    m_shortcutEdit->setText(keySequenceToEditString(key));
}

QKeySequence ShortcutInput::keySequence() const
{
    return keySequenceFromEditString(m_shortcutEdit->text());
}

void ShortcutInput::setConflictChecker(const ShortcutInput::ConflictChecker &fun)
{
    m_conflictChecker = fun;
}

class ShortcutSettingsWidget final : public CommandMappings
{
public:
    ShortcutSettingsWidget();
    ~ShortcutSettingsWidget() final;

    void apply();

private:
    void importAction() final;
    void exportAction() final;
    void defaultAction() final;
    bool filterColumn(const QString &filterString, QTreeWidgetItem *item, int column) const final;

    void initialize();
    void handleCurrentCommandChanged(QTreeWidgetItem *current);
    void resetToDefault();
    bool updateAndCheckForConflicts(const QKeySequence &key, int index) const;
    bool markCollisions(ShortcutItem *, int index);
    void markAllCollisions();
    void showConflicts();
    void clear();

    void setupShortcutBox(ShortcutItem *scitem);

    QList<ShortcutItem *> m_scitems;
    QGroupBox *m_shortcutBox;
    QGridLayout *m_shortcutLayout;
    std::vector<std::unique_ptr<ShortcutInput>> m_shortcutInputs;
    QPointer<QPushButton> m_addButton = nullptr;
    QTimer m_updateTimer;
};

ShortcutSettingsWidget::ShortcutSettingsWidget()
{
    setPageTitle(Tr::tr("Keyboard Shortcuts"));
    setTargetHeader(Tr::tr("Shortcut"));
    setResetVisible(true);

    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(100);

    connect(ActionManager::instance(), &ActionManager::commandListChanged,
            &m_updateTimer, qOverload<>(&QTimer::start));
    connect(&m_updateTimer, &QTimer::timeout,
            this, &ShortcutSettingsWidget::initialize);
    connect(this, &ShortcutSettingsWidget::currentCommandChanged,
            this, &ShortcutSettingsWidget::handleCurrentCommandChanged);
    connect(this, &ShortcutSettingsWidget::resetRequested,
            this, &ShortcutSettingsWidget::resetToDefault);

    m_shortcutBox = new QGroupBox(Tr::tr("Shortcut"), this);
    m_shortcutBox->setEnabled(false);
    m_shortcutLayout = new QGridLayout(m_shortcutBox);
    m_shortcutBox->setLayout(m_shortcutLayout);
    layout()->addWidget(m_shortcutBox);

    initialize();
}

ShortcutSettingsWidget::~ShortcutSettingsWidget()
{
    qDeleteAll(m_scitems);
}

void ShortcutSettingsWidget::apply()
{
    for (const ShortcutItem *item : std::as_const(m_scitems))
        item->m_cmd->setKeySequences(item->m_keys);
}

ShortcutItem *shortcutItem(QTreeWidgetItem *treeItem)
{
    if (!treeItem)
        return nullptr;
    return treeItem->data(0, Qt::UserRole).value<ShortcutItem *>();
}

void ShortcutSettingsWidget::handleCurrentCommandChanged(QTreeWidgetItem *current)
{
    ShortcutItem *scitem = shortcutItem(current);
    if (!scitem) {
        m_shortcutInputs.clear();
        delete m_addButton;
        m_shortcutBox->setEnabled(false);
    } else {
        // clean up before showing UI
        scitem->m_keys = cleanKeys(scitem->m_keys);
        setupShortcutBox(scitem);
        m_shortcutBox->setEnabled(true);
    }
}

void ShortcutSettingsWidget::setupShortcutBox(ShortcutItem *scitem)
{
    const auto updateAddButton = [this] {
        m_addButton->setEnabled(
            !Utils::anyOf(m_shortcutInputs, [](const std::unique_ptr<ShortcutInput> &i) {
                return i->keySequence().isEmpty();
            }));
    };
    const auto addShortcutInput = [this, updateAddButton](int index, const QKeySequence &key) {
        auto input = std::make_unique<ShortcutInput>();
        input->addToLayout(m_shortcutLayout, index * 2);
        input->setConflictChecker(
            [this, index](const QKeySequence &k) { return updateAndCheckForConflicts(k, index); });
        connect(input.get(),
                &ShortcutInput::showConflictsRequested,
                this,
                &ShortcutSettingsWidget::showConflicts);
        connect(input.get(), &ShortcutInput::changed, this, updateAddButton);
        input->setKeySequence(key);
        m_shortcutInputs.push_back(std::move(input));
    };
    const auto addButtonToLayout = [this, updateAddButton] {
        m_shortcutLayout->addWidget(m_addButton,
                                    int(m_shortcutInputs.size() * 2 - 1),
                                    m_shortcutLayout->columnCount() - 1);
        updateAddButton();
    };
    m_shortcutInputs.clear();
    delete m_addButton;
    m_addButton = new QPushButton(Tr::tr("Add"), this);
    for (int i = 0; i < qMax(1, scitem->m_keys.size()); ++i)
        addShortcutInput(i, scitem->m_keys.value(i));
    connect(m_addButton, &QPushButton::clicked, this, [this, addShortcutInput, addButtonToLayout] {
        addShortcutInput(int(m_shortcutInputs.size()), {});
        addButtonToLayout();
    });
    addButtonToLayout();
    updateAddButton();
}

bool ShortcutSettingsWidget::updateAndCheckForConflicts(const QKeySequence &key, int index) const
{
    QTreeWidgetItem *current = commandList()->currentItem();
    ShortcutItem *item = shortcutItem(current);
    if (!item)
        return false;

    while (index >= item->m_keys.size())
        item->m_keys.append(QKeySequence());
    item->m_keys[index] = key;
    CommandMappings::setModified(current,
                                 cleanKeys(item->m_keys) != item->m_cmd->defaultKeySequences());
    current->setText(2, keySequencesToNativeString(item->m_keys));
    auto that = const_cast<ShortcutSettingsWidget *>(this);
    return that->markCollisions(item, index);
}

bool ShortcutSettingsWidget::filterColumn(const QString &filterString, QTreeWidgetItem *item,
                                          int column) const
{
    const ShortcutItem *scitem = shortcutItem(item);
    if (column == item->columnCount() - 1) { // shortcut
        // filter on the shortcut edit text
        if (!scitem)
            return true;
        const QStringList filters = Utils::transform(filterString.split(kSeparator),
                                                     [](const QString &s) { return s.trimmed(); });
        for (const QKeySequence &k : scitem->m_keys) {
            const QString &keyString = keySequenceToEditString(k);
            const bool found = Utils::anyOf(filters, [keyString](const QString &f) {
                return keyString.contains(f, Qt::CaseInsensitive);
            });
            if (found)
                return false;
        }
        return true;
    }
    QString text;
    if (column == 0 && scitem) { // command id
        text = scitem->m_cmd->id().toString();
    } else {
        text = item->text(column);
    }
    return !text.contains(filterString, Qt::CaseInsensitive);
}

void ShortcutSettingsWidget::showConflicts()
{
    QTreeWidgetItem *current = commandList()->currentItem();
    ShortcutItem *scitem = shortcutItem(current);
    if (scitem)
        setFilterText(keySequencesToEditString(scitem->m_keys));
}

void ShortcutSettingsWidget::resetToDefault()
{
    QTreeWidgetItem *current = commandList()->currentItem();
    ShortcutItem *scitem = shortcutItem(current);
    if (scitem) {
        scitem->m_keys = scitem->m_cmd->defaultKeySequences();
        current->setText(2, keySequencesToNativeString(scitem->m_keys));
        CommandMappings::setModified(current, false);
        setupShortcutBox(scitem);
        markAllCollisions();
    }
}

void ShortcutSettingsWidget::importAction()
{
    FilePath fileName = FileUtils::getOpenFilePath(Tr::tr("Import Keyboard Mapping Scheme"),
                                                   schemesPath(),
                                                   Tr::tr("Keyboard Mapping Scheme (*.kms)"));
    if (!fileName.isEmpty()) {

        CommandsFile cf(fileName);
        QMap<QString, QList<QKeySequence>> mapping = cf.importCommands();
        for (ShortcutItem *item : std::as_const(m_scitems)) {
            QString sid = item->m_cmd->id().toString();
            if (mapping.contains(sid)) {
                item->m_keys = mapping.value(sid);
                item->m_item->setText(2, keySequencesToNativeString(item->m_keys));
                if (item->m_item == commandList()->currentItem())
                    emit currentCommandChanged(item->m_item);

                if (item->m_keys != item->m_cmd->defaultKeySequences())
                    setModified(item->m_item, true);
                else
                    setModified(item->m_item, false);
            }
        }
        markAllCollisions();
    }
}

void ShortcutSettingsWidget::defaultAction()
{
    for (ShortcutItem *item : std::as_const(m_scitems)) {
        item->m_keys = item->m_cmd->defaultKeySequences();
        item->m_item->setText(2, keySequencesToNativeString(item->m_keys));
        setModified(item->m_item, false);
        if (item->m_item == commandList()->currentItem())
            emit currentCommandChanged(item->m_item);
    }
    markAllCollisions();
}

void ShortcutSettingsWidget::exportAction()
{
    const FilePath filePath
        = DocumentManager::getSaveFileNameWithExtension(Tr::tr("Export Keyboard Mapping Scheme"),
                                                        schemesPath(),
                                                        Tr::tr("Keyboard Mapping Scheme (*.kms)"));
    if (!filePath.isEmpty()) {
        CommandsFile cf(filePath);
        cf.exportCommands(m_scitems);
    }
}

void ShortcutSettingsWidget::clear()
{
    QTreeWidget *tree = commandList();
    for (int i = tree->topLevelItemCount()-1; i >= 0 ; --i) {
        delete tree->takeTopLevelItem(i);
    }
    qDeleteAll(m_scitems);
    m_scitems.clear();
}

void ShortcutSettingsWidget::initialize()
{
    clear();
    QMap<QString, QTreeWidgetItem *> sections;

    const QList<Command *> commands = ActionManager::commands();
    for (Command *c : commands) {
        if (c->hasAttribute(Command::CA_NonConfigurable))
            continue;
        if (c->action() && c->action()->isSeparator())
            continue;

        QTreeWidgetItem *item = nullptr;
        auto s = new ShortcutItem;
        m_scitems << s;
        item = new QTreeWidgetItem;
        s->m_cmd = c;
        s->m_item = item;

        const QString identifier = c->id().toString();
        int pos = identifier.indexOf(QLatin1Char('.'));
        const QString section = identifier.left(pos);
        const QString subId = identifier.mid(pos + 1);
        if (!sections.contains(section)) {
            QTreeWidgetItem *categoryItem = new QTreeWidgetItem(commandList(), QStringList(section));
            QFont f = categoryItem->font(0);
            f.setBold(true);
            categoryItem->setFont(0, f);
            sections.insert(section, categoryItem);
            commandList()->expandItem(categoryItem);
        }
        sections[section]->addChild(item);

        s->m_keys = c->keySequences();
        item->setText(0, subId);
        item->setText(1, c->description());
        item->setText(2, keySequencesToNativeString(s->m_keys));
        if (s->m_keys != s->m_cmd->defaultKeySequences())
            setModified(item, true);

        item->setData(0, Qt::UserRole, QVariant::fromValue(s));
    }
    markAllCollisions();
    filterChanged(filterText());
}

bool ShortcutSettingsWidget::markCollisions(ShortcutItem *item, int index)
{
    bool hasCollision = false;
    const QKeySequence key = item->m_keys.value(index);
    if (!key.isEmpty()) {
        Id globalId(Constants::C_GLOBAL);
        const Context itemContext = item->m_cmd->context();
        const bool itemHasGlobalContext = itemContext.contains(globalId);
        for (ShortcutItem *currentItem : std::as_const(m_scitems)) {
            if (item == currentItem)
                continue;
            if (!Utils::anyOf(currentItem->m_keys, Utils::equalTo(key)))
                continue;
            // check if contexts might conflict
            const Context currentContext = currentItem->m_cmd->context();
            bool currentIsConflicting = (itemHasGlobalContext && !currentContext.isEmpty());
            if (!currentIsConflicting) {
                for (const Id &id : currentContext) {
                    if ((id == globalId && !itemContext.isEmpty()) || itemContext.contains(id)) {
                        currentIsConflicting = true;
                        break;
                    }
                }
            }
            if (currentIsConflicting) {
                currentItem->m_item->setForeground(2,
                                                   Utils::creatorColor(
                                                   Utils::Theme::TextColorError));
                hasCollision = true;
            }
        }
    }
    item->m_item->setForeground(2,
                                hasCollision
                                    ? Utils::creatorColor(Utils::Theme::TextColorError)
                                    : commandList()->palette().windowText());
    return hasCollision;
}

void ShortcutSettingsWidget::markAllCollisions()
{
    for (ShortcutItem *item : std::as_const(m_scitems))
        for (int i = 0; i < item->m_keys.size(); ++i)
            markCollisions(item, i);
}

// ShortcutSettingsPageWidget

class ShortcutSettingsPageWidget final : public IOptionsPageWidget
{
public:
    ShortcutSettingsPageWidget()
    {
        auto inner = new ShortcutSettingsWidget;
        auto vbox = new QVBoxLayout(this);
        vbox->addWidget(inner);
        vbox->setContentsMargins(0, 0, 0, 0);

        setOnApply([inner] { inner->apply(); });
    }
};

// ShortcutSettings

class ShortcutSettings final : public IOptionsPage
{
public:
    ShortcutSettings()
    {
        setId(Constants::SETTINGS_ID_SHORTCUTS);
        setDisplayName(Tr::tr("Keyboard"));
        setCategory(Constants::SETTINGS_CATEGORY_CORE);
        setWidgetCreator([] { return new ShortcutSettingsPageWidget; });
    }
};

void setupShortcutSettings()
{
    static ShortcutSettings theShortcutSettings;
}

} // namespace Core::Internal

#include "shortcutsettings.moc"
