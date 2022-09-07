// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "simplefiledialog.h"

#include "../filepath.h"
#include "fileiconprovider.h"

#include <QAbstractItemModel>
#include <QCompleter>
#include <QFuture>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QSettings>

namespace Utils {

namespace Internal {
class SimpleModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    ~SimpleModel() override = default;

    void setRoot(const FilePath &rootPath)
    {
        if (rootPath == m_root)
            return;

        beginResetModel();
        m_root = rootPath;
        m_filePaths.clear();
        m_icons.clear();
        endResetModel();

        QtConcurrent::run([rootPath]() {
            const auto dirs = rootPath.dirEntries({QStringList() << "*",
                                                   QDir::System | QDir::Dirs | QDir::Drives
                                                       | QDir::NoDotAndDotDot},
                                                  QDir::Name);
            const auto files = rootPath.dirEntries({QStringList() << "*", QDir::Files}, QDir::Name);

            return dirs + files;
        }).then(this, [this, rootPath](const FilePaths &filePaths) {
            if (rootPath != m_root)
                return;
            beginResetModel();
            m_filePaths = filePaths;
            if (rootPath != FilePath::rootPath())
                m_filePaths.prepend(rootPath.pathAppended(".."));

            endResetModel();

            QtConcurrent::run([this, rootPath]() {
                return QtConcurrent::blockingMapped(m_filePaths, [](const FilePath &filePath) {
                    return FileIconProvider::icon(filePath);
                });
            }).then(this, [this, rootPath](const QList<QIcon> &icons) {
                if (rootPath != m_root)
                    return;
                m_icons = icons;
                emit dataChanged(index(0, 0), index(m_filePaths.size() - 1), {Qt::DecorationRole});
            });
        });
    }

    FilePath root() const { return m_root; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_filePaths.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role == Qt::DisplayRole) {
            const auto &filePath = m_filePaths.at(index.row());
            if (filePath.fileName().isEmpty())
                return filePath.host().toString();
            return filePath.fileName();
        }
        if (role == Qt::UserRole + 1 || role == Qt::EditRole) {
            return m_filePaths.at(index.row()).toString();
        }
        if (role == Qt::DecorationRole) {
            if (m_icons.size() > index.row())
                return m_icons.at(index.row());
            return Utils::FileIconProvider::icon(QAbstractFileIconProvider::File);
        }
        return QVariant();
    }

private:
    FilePath m_root;
    FilePaths m_filePaths;
    QList<QIcon> m_icons;
    QFutureWatcher<void> m_fetchWatch;
};

class ListView : public QListView
{
public:
    using QListView::QListView;

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            emit activated(currentIndex());
        }

        QListView::keyPressEvent(event);
    }
};

class EventFilter : public QObject
{
public:
    EventFilter(std::function<bool(QEvent *)> handler, QObject *parent)
        : QObject(parent)
        , m_handler(std::move(handler))
    {}

    bool eventFilter(QObject *, QEvent *event) { return m_handler(event); }

private:
    std::function<bool(QEvent *)> m_handler;
};

class LineEdit : public QLineEdit
{
public:
    LineEdit(QWidget *parent, std::function<void(Qt::Key)> keyHandler)
        : QLineEdit(parent)
        , m_keyHandler(std::move(keyHandler))
    {}

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up
            || event->key() == Qt::Key_Tab) {
            m_keyHandler((Qt::Key) event->key());
        }

        QLineEdit::keyPressEvent(event);
    }

private:
    std::function<void(Qt::Key)> m_keyHandler;
};

} // namespace Internal

SimpleFileDialog::SimpleFileDialog(QWidget *parent)
    : QDialog(parent)
{
    QSettings settings;
    QString lastDir = settings.value("SimpleFileDialog/LastDir").toString();
    if (lastDir.isEmpty())
        lastDir = QDir::homePath();

    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto model = new Internal::SimpleModel(this);
    model->setRoot(FilePath::fromString(lastDir));

    auto completer = new QCompleter(model);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::InlineCompletion);

    auto listView = new Internal::ListView(this);
    listView->setModel(model);
    listView->setFocusPolicy(Qt::NoFocus);

    auto lineEdit = new Internal::LineEdit(this, [listView](Qt::Key key) {
        QModelIndex current = listView->selectionModel()->currentIndex();
        if (key == Qt::Key_Down) {
            if (current.row() < listView->model()->rowCount() - 1) {
                current = listView->model()->index(current.row() + 1, 0);
            } else {
                current = listView->model()->index(0, 0);
            }
            listView->selectionModel()->setCurrentIndex(current,
                                                        QItemSelectionModel::ClearAndSelect
                                                            | QItemSelectionModel::Current);
        } else if (key == Qt::Key_Up) {
            if (current.row() > 0) {
                current = listView->model()->index(current.row() - 1, 0);
            } else {
                current = listView->model()->index(listView->model()->rowCount() - 1, 0);
            }
            listView->selectionModel()->setCurrentIndex(current,
                                                        QItemSelectionModel::ClearAndSelect
                                                            | QItemSelectionModel::Current);
        } else if (key == Qt::Key_Tab && current.isValid()) {
            listView->activated(current);
        }
    });

    lineEdit->setText(model->root().toString());

    auto errorLabel = new QLabel(this);
    errorLabel->setVisible(false);

    QFont f = lineEdit->font();
    f.setPointSizeF(f.pointSizeF()*1.5);
    lineEdit->setFont(f);
    listView->setGridSize(listView->gridSize() * 2);
    listView->setFont(f);
    listView->setIconSize({32, 32});
    errorLabel->setFont(f);

    layout->addWidget(lineEdit);
    layout->addWidget(errorLabel);
    layout->addWidget(listView);

    /*lineEdit->installEventFilter(new Internal::EventFilter(
        [lineEdit](QEvent *event) {
            if (event->type() == QEvent::KeyPress) {
                auto keyEvent = static_cast<QKeyEvent *>(event);
                if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Tab) {
                    if (lineEdit->selectionLength() > 0) {
                        lineEdit->deselect();
                        lineEdit->setCursorPosition(lineEdit->text().size());
                        return true;
                    }
                }
            }
            return false;
        },
        lineEdit));*/

    connect(lineEdit, &QLineEdit::returnPressed, this, [listView]() {
        listView->activated(listView->currentIndex());
    });

    connect(lineEdit,
            &QLineEdit::textChanged,
            this,
            [model, completer, listView, errorLabel](const QString &text) {
                const QString path = text.left(text.lastIndexOf('/') + 1);
                const FilePath fPath = FilePath::fromString(path).cleanPath();
                if (fPath != model->root()) {
                    if (fPath.isDir()) {
                        QSettings settings;
                        settings.setValue("SimpleFileDialog/LastDir", fPath.toFSPathString());
                        model->setRoot(fPath);
                        errorLabel->setVisible(false);
                    }
                }
                completer->setCompletionPrefix(text);
                QModelIndex current = completer->currentIndex();
                if (current.isValid()) {
                    QModelIndex srcIndex = qobject_cast<QAbstractProxyModel *>(
                                               completer->completionModel())
                                               ->mapToSource(current);
                    listView->selectionModel()->setCurrentIndex(srcIndex,
                                                                QItemSelectionModel::ClearAndSelect
                                                                    | QItemSelectionModel::Current);
                    listView->scrollTo(srcIndex);
                } else {
                    listView->selectionModel()->clearSelection();
                    listView->selectionModel()->clearCurrentIndex();
                }
            });

    connect(listView,
            &QListView::activated,
            this,
            [this, errorLabel, lineEdit, listView](const QModelIndex &) {
                const auto currentIndex = listView->selectionModel()->currentIndex();
                if (currentIndex.isValid()) {
                    QString p = currentIndex.data(Qt::UserRole + 1).toString();
                    FilePath fp = FilePath::fromUserInput(p).cleanPath();
                    if (fp.isDir()) {
                        lineEdit->setFocus();
                        QString newText = fp.toUserOutput();
                        if (!newText.endsWith('/'))
                            newText += '/';
                        lineEdit->setText(newText);
                    } else if (fp.isFile()) {
                        m_selectedFilePath = fp;
                        accept();
                    }
                } else {
                    errorLabel->setText(tr("Please enter an existing path."));
                    errorLabel->setVisible(true);
                }
            });

    setMinimumSize(400, 300);
}

FilePath SimpleFileDialog::selectedFilePath() const
{
    return m_selectedFilePath;
}

} // namespace Utils
