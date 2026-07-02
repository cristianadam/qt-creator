// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QMetaType>
#include <QProcess>

#include <functional>

namespace Utils {

// Process state / exit-status / error / channel-mode enums.
//
// Where Qt provides QProcess (QT_CONFIG(process)) these are kept as aliases of the
// matching QProcess enums, and the enumerators are pulled into the Utils namespace via
// "using enum". That way the large body of existing desktop code that spells these
// "QProcess::NormalExit" keeps compiling unchanged and stays type-compatible with the
// Process API, while code may also use the platform-independent "Utils::" spelling.
//
// On WebAssembly QProcess (and its enums) are compiled out (QT_FEATURE_process == -1),
// so we provide Utils-owned equivalents with identical underlying values. These are
// intentionally *unscoped* (unlike the scoped enums below) to stay drop-in compatible
// with the unscoped QProcess enums on desktop.
#if QT_CONFIG(process)

using ProcessState = QProcess::ProcessState;
using ProcessExitStatus = QProcess::ExitStatus;
using ProcessError = QProcess::ProcessError;
using ProcessChannelMode = QProcess::ProcessChannelMode;
using enum QProcess::ProcessState;       // NotRunning, Starting, Running
using enum QProcess::ExitStatus;         // NormalExit, CrashExit
using enum QProcess::ProcessError;       // FailedToStart, Crashed, Timedout, ReadError, WriteError, UnknownError
using enum QProcess::ProcessChannelMode; // SeparateChannels, MergedChannels, Forwarded*

#else // WebAssembly: QProcess is unavailable.

enum ProcessState { NotRunning, Starting, Running };
enum ProcessExitStatus { NormalExit, CrashExit };
enum ProcessError { FailedToStart, Crashed, Timedout, ReadError, WriteError, UnknownError };
enum ProcessChannelMode {
    SeparateChannels,
    MergedChannels,
    ForwardedChannels,
    ForwardedOutputChannel,
    ForwardedErrorChannel
};

#endif // QT_CONFIG(process)

enum class ProcessMode {
    Reader, // This opens in ReadOnly mode if no write data or in ReadWrite mode otherwise,
            // closes the write channel afterwards.
    Writer  // This opens in ReadWrite mode and doesn't close the write channel
};

enum class TerminalMode {
    Off,
    Run,      // Start with process stub enabled
    Debug,    // Start with process stub enabled and wait for debugger to attach
    Detached, // Start in a terminal, without process stub.
};

// Miscellaneous, not process core

enum class Channel {
    Output,
    Error
};

enum class DetachedChannelMode {
    Forward,
    Discard
};

enum class TextChannelMode {
                // Keep | Emit | Emit
                //  raw | text | content
                // data |  sig |
                // -----+------+--------
    Off,        //  yes |   no | -
    SingleLine, //   no |  yes | Single lines
    MultiLine   //  yes |  yes | All the available data
};

enum class ProcessResult {
    // Finished successfully. Unless an ExitCodeInterpreter is set
    // this corresponds to a return code 0.
    FinishedWithSuccess,
    // Finished unsuccessfully. Unless an ExitCodeInterpreter is set
    // this corresponds to a return code different from 0.
    FinishedWithError,
    // Process terminated abnormally (crash)
    TerminatedAbnormally,
    // Executable could not be started
    StartFailed,
    // Canceled due to a call to terminate() or kill(),
    // This includes a call to stop() or timeout has triggered for runBlocking().
    Canceled
};

using TextChannelCallback = std::function<void(const QString & /*text*/)>;

} // namespace Utils

Q_DECLARE_METATYPE(Utils::ProcessMode);
