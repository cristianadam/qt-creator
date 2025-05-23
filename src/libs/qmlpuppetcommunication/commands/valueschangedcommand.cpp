// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "valueschangedcommand.h"

#include "sharedmemory.h"

#include <QCache>
#include <QDebug>
#include <QIODevice>

#include <cstring>

#include <algorithm>

namespace QmlDesigner {

// using cache as a container which deletes sharedmemory pointers at process exit
using GlobalSharedMemoryContainer = QCache<qint32, SharedMemory>;
Q_GLOBAL_STATIC_WITH_ARGS(GlobalSharedMemoryContainer, globalSharedMemoryContainer, (10000))

ValuesChangedCommand::ValuesChangedCommand()
    : m_keyNumber(0)
{
}

ValuesChangedCommand::ValuesChangedCommand(const QList<PropertyValueContainer> &valueChangeVector)
    : m_valueChangeVector (valueChangeVector),
      m_keyNumber(0)
{
}

const QList<PropertyValueContainer> ValuesChangedCommand::valueChanges() const
{
    return m_valueChangeVector;
}

quint32 ValuesChangedCommand::keyNumber() const
{
    return m_keyNumber;
}

void ValuesChangedCommand::removeSharedMemorys(const QList<qint32> &keyNumberVector)
{
    for (qint32 keyNumber : keyNumberVector) {
        SharedMemory *sharedMemory = globalSharedMemoryContainer()->take(keyNumber);
        delete sharedMemory;
    }
}

void ValuesChangedCommand::sort()
{
    std::sort(m_valueChangeVector.begin(), m_valueChangeVector.end());
}

static const QLatin1String valueKeyTemplateString("Values-%1");

static SharedMemory *createSharedMemory(qint32 key, int byteCount)
{
    SharedMemory *sharedMemory = new SharedMemory(QString(valueKeyTemplateString).arg(key));

    bool sharedMemoryIsCreated = sharedMemory->create(byteCount);

    if (sharedMemoryIsCreated) {
        globalSharedMemoryContainer()->insert(key, sharedMemory);
        return sharedMemory;
    } else {
        delete sharedMemory;
    }

    return nullptr;
}

QDataStream &operator<<(QDataStream &out, const ValuesChangedCommand &command)
{
    static const bool dontUseSharedMemory = qEnvironmentVariableIsSet("DESIGNER_DONT_USE_SHARED_MEMORY");

    QList<PropertyValueContainer> propertyValueContainer = command.valueChanges();

    if (command.transactionOption != ValuesChangedCommand::TransactionOption::None) {
        PropertyValueContainer optionContainer(command.transactionOption);
        propertyValueContainer.append(optionContainer);
    }

    if (!dontUseSharedMemory && propertyValueContainer.count() > 5) {
        static quint32 keyCounter = 0;
        ++keyCounter;
        command.m_keyNumber = keyCounter;
        QByteArray outDataStreamByteArray;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QDataStream temporaryOutDataStream(&outDataStreamByteArray, QIODevice::WriteOnly);
#else
        QDataStream temporaryOutDataStream(&outDataStreamByteArray, QDataStream::WriteOnly);
#endif
        temporaryOutDataStream.setVersion(QDataStream::Qt_4_8);

        temporaryOutDataStream << propertyValueContainer;

        SharedMemory *sharedMemory = createSharedMemory(keyCounter, outDataStreamByteArray.size());

        if (sharedMemory) {
            sharedMemory->lock();
            std::memcpy(sharedMemory->data(), outDataStreamByteArray.constData(), sharedMemory->size());
            sharedMemory->unlock();

            out << command.keyNumber();
            return out;
        }
    }

    out << qint32(0);
    out << propertyValueContainer;

    return out;
}

void readSharedMemory(qint32 key, QList<PropertyValueContainer> *valueChangeVector)
{
    SharedMemory sharedMemory(QString(valueKeyTemplateString).arg(key));
    bool canAttach = sharedMemory.attach(QSharedMemory::ReadOnly);

    if (canAttach) {
        sharedMemory.lock();

        QDataStream in(QByteArray::fromRawData(static_cast<const char*>(sharedMemory.constData()), sharedMemory.size()));
        in.setVersion(QDataStream::Qt_4_8);
        in >> *valueChangeVector;

        sharedMemory.unlock();
        sharedMemory.detach();
    }
}

QDataStream &operator>>(QDataStream &in, ValuesChangedCommand &command)
{
    in >> command.m_keyNumber;

    QList<PropertyValueContainer> valueChangeVector;

    if (command.keyNumber() > 0)
        readSharedMemory(command.keyNumber(), &valueChangeVector);
    else
        in >> valueChangeVector;

    // '-option-' is not a valid property name and indicates that we store the transaction option.
    if (!valueChangeVector.isEmpty() && valueChangeVector.last().name() == "-option-") {
        command.transactionOption =
            static_cast<ValuesChangedCommand::TransactionOption>(valueChangeVector.last().instanceId());
        valueChangeVector.removeLast();
    }

    command.m_valueChangeVector = valueChangeVector;

    return in;
}

bool operator ==(const ValuesChangedCommand &first, const ValuesChangedCommand &second)
{
    return first.m_valueChangeVector == second.m_valueChangeVector
        && first.transactionOption == second.transactionOption;
}

QDebug operator <<(QDebug debug, const ValuesChangedCommand &command)
{
    return debug.nospace() << "ValuesChangedCommand("
                    << "keyNumber: " << command.keyNumber() << ", "
                    << command.valueChanges() << ")";
}

QDataStream &operator<<(QDataStream &out, const ValuesModifiedCommand &command)
{
    return out << static_cast<const ValuesChangedCommand &>(command);
}

QDataStream &operator>>(QDataStream &in, ValuesModifiedCommand &command)
{
    return in >> static_cast<ValuesChangedCommand &>(command);
}


} // namespace QmlDesigner
