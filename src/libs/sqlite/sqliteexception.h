// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "sqliteglobal.h"

#include <utils/smallstring.h>

#include <exception>
#include <iostream>

namespace Sqlite {

class SQLITE_EXPORT Exception : public std::exception
{
public:
    Exception() = default;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ExceptionWithMessage : public Exception
{
public:
    ExceptionWithMessage(Utils::SmallString &&sqliteErrorMessage = Utils::SmallString{})
        : m_sqliteErrorMessage(std::move(sqliteErrorMessage))
    {}

    const char *what() const noexcept override;
    void printWarning() const;

private:
    Utils::SmallString m_sqliteErrorMessage;
};

class SQLITE_EXPORT StatementIsBusy : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT StatementIsBusyRecovering : public StatementIsBusy
{
public:
    using StatementIsBusy::StatementIsBusy;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT StatementIsBusySnapshot : public StatementIsBusy
{
public:
    using StatementIsBusy::StatementIsBusy;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT StatementIsBusyTimeout : public StatementIsBusy
{
public:
    using StatementIsBusy::StatementIsBusy;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsBusy : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsBusyRecovering : public DatabaseIsBusy
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsBusySnapshot : public DatabaseIsBusy
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsBusyTimeout : public DatabaseIsBusy
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT StatementHasError : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT StatementIsMisused : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputError : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotAuthenticate : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotBeginAtomic : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotCommitAtomic : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotRollbackAtomic : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputDataError : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputBlocked : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputFileSystemIsCorrupt : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputVNodeError : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputConvPathFailed : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotGetTemporaryPath : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotMemoryMap : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotDeleteNonExistingFile : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotSeek : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotMapSharedMemory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotLockSharedMemory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotEnlargeSharedMemory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotOpenSharedMemory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotCloseDirectory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotClose : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotLock : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotCheckReservedLock : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotAccess : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputNoMemory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotDelete : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotReadLock : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotUnlock : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotFsStat : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotTruncate : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotSynchronizeDirectory : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotSynchronizeFile : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotWrite : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotShortRead : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutputCannotRead : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT InputOutput : public InputOutputError
{
public:
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ConstraintPreventsModification : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT NoValuesToFetch : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT BindingIndexIsOutOfRange : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT WrongBindingName : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsNotOpen : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseCannotBeOpened : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseFilePathIsEmpty : public DatabaseCannotBeOpened
{
public:
    using DatabaseCannotBeOpened::DatabaseCannotBeOpened;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsAlreadyOpen : public DatabaseCannotBeOpened
{
public:
    using DatabaseCannotBeOpened::DatabaseCannotBeOpened;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseCannotBeClosed : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsAlreadyClosed : public DatabaseCannotBeClosed
{
public:
    using DatabaseCannotBeClosed::DatabaseCannotBeClosed;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT WrongFilePath : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT PragmaValueNotSet : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT PragmaValueCannotBeTransformed : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT NotReadOnlySqlStatement : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT NotWriteSqlStatement : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DeadLock : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT UnknowError : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT BindingTooBig : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT TooBig : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CannotConvert : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ForeignKeyColumnIsNotUnique : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CannotApplyChangeSet : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ChangeSetIsMisused : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT SchemeChangeError : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CannotWriteToReadOnlyConnection : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ProtocolError : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseExceedsMaximumFileSize : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DataTypeMismatch : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ConnectionIsLocked : public ExceptionWithMessage
{
public:
    using ExceptionWithMessage::ExceptionWithMessage;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ExecutionInterrupted : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsCorrupt : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CannotOpen : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CannotCreateChangeSetIterator : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CannotGetChangeSetOperation : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ChangeSetTupleIsOutOfRange : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT ChangeSetTupleIsMisused : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT UnknownError : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT DatabaseIsNotLocked : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT WrongBindingParameterCount : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT WrongColumnCount : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT IndexHasNoTableName : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT IndexHasNoColumns : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT MultiTheadingCannotBeActivated : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT LoggingCannotBeActivated : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT MemoryMappingCannotBeChanged : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT LibraryCannotBeInitialized : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT LibraryCannotBeShutdown : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT LogCannotBeCheckpointed : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT BusyTimerCannotBeSet : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

class SQLITE_EXPORT CheckpointIsMisused : public Exception
{
public:
    using Exception::Exception;
    const char *what() const noexcept override;
};

} // namespace Sqlite
