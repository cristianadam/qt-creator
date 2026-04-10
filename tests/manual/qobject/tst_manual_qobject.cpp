// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QApplication>
#include <QWidget>

class Middle : public QObject
{
    Q_OBJECT

public:
    Middle() {}

    ~Middle()
    {
        emit buh();
    }

signals:
    void buh();

};

class Parent : public Middle
{
    Q_OBJECT

public:
    Parent()
    {
    }

    ~Parent()
    {
    }

public slots:

    void slot()
    {
        qDebug() << data;
    }

private:
    QList<int> data { 1, 2, 3, 4 };
};


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Parent parent;

    // Triggers the problem:
    // QObject::connect(&parent, &Middle::buh, &parent, [&] { parent.slot(); });

    // Warn in debug build:
    // ASSERT failure in Parent: "Called object is not of the correct type
    // (class destructor may have already run)", file /data/dev/qt-6/qtbase/src/corelib/kernel/qobjectdefs_impl.h, line 105
    QObject::connect(&parent, &Middle::buh, &parent, &Parent::slot);

    // Connection does not trigger, no problem.
    // QObject::connect(&parent, SIGNAL(buh()), &parent, SLOT(slot()));

    return 0;
}


#include "tst_manual_qobject.moc"
