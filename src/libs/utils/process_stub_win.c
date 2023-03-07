// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 /* WinXP, needed for DebugActiveProcessStop() */

#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <direct.h>

static FILE *qtcFd;
static wchar_t *sleepMsg;

enum RunMode { Run, Debug };

/* Print some "press enter" message, wait for that, exit. */
static void doExit(int code)
{
    char buf[2];
    _putws(sleepMsg);
    fgets(buf, 2, stdin); /* Minimal size to make it wait */
    exit(code);
}

/* Print an error message for unexpected Windows system errors, wait, exit. */
static void systemError(const char *str)
{
    fprintf(stderr, str, GetLastError());
    doExit(3);
}

/* Send a message to the master. */
static void sendMsg(const char *msg, int num)
{
    int pidStrLen;
    char pidStr[64];

    pidStrLen = sprintf(pidStr, msg, num);
    if (fwrite(pidStr, pidStrLen, 1, qtcFd) != 1 || fflush(qtcFd)) {
        fprintf(stderr, "Cannot write to creator comm socket: %s\n",
                strerror(errno));
        doExit(3);
    }
}

/* Ignore the first ctrl-c/break within a second. */
static BOOL WINAPI ctrlHandler(DWORD dwCtrlType)
{
    static ULARGE_INTEGER lastTime;
    ULARGE_INTEGER thisTime;
    SYSTEMTIME sysTime;
    FILETIME fileTime;

    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        GetSystemTime(&sysTime);
        SystemTimeToFileTime(&sysTime, &fileTime);
        thisTime.LowPart = fileTime.dwLowDateTime;
        thisTime.HighPart = fileTime.dwHighDateTime;
        if (lastTime.QuadPart + 10000000 < thisTime.QuadPart) {
            lastTime.QuadPart = thisTime.QuadPart;
            return TRUE;
        }
    }
    return FALSE;
}

enum {
    ArgCmd = 0,
    ArgAction,
    ArgSocket,
    ArgDir,
    ArgEnv,
    ArgCmdLine,
    ArgMsg,
    ArgCount
};

/* syntax: $0 {"run"|"debug"} <pid-socket> <workdir> <env-file> <cmdline> <continuation-msg> */
/* exit codes: 0 = ok, 1 = invocation error, 3 = internal error */
int main()
{
    int argc;
    int creationFlags;
    wchar_t **argv;
    wchar_t *env = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    enum RunMode mode = Run;

    argv = CommandLineToArgvW(GetCommandLine(), &argc);

    if (argc != ArgCount) {
        fprintf(stderr, "This is an internal helper of Qt Creator. Do not run it manually.\n");
        return 1;
    }
    sleepMsg = argv[ArgMsg];

    /* Connect to the master, i.e. Creator. */
    if (!(qtcFd = _wfopen(argv[ArgSocket], L"w"))) {
        fprintf(stderr, "Cannot connect creator comm pipe %S: %s\n",
                argv[ArgSocket], strerror(errno));
        doExit(1);
    }

    if (*argv[ArgDir] && !SetCurrentDirectoryW(argv[ArgDir])) {
        /* Only expected error: no such file or direcotry */
        sendMsg("err:chdir %d\n", GetLastError());
        return 1;
    }

    if (*argv[ArgEnv]) {
        FILE *envFd;
        long size;
        if (!(envFd = _wfopen(argv[ArgEnv], L"rb"))) {
            fprintf(stderr, "Cannot read creator env file %S: %s\n",
                    argv[ArgEnv], strerror(errno));
            doExit(1);
        }
        fseek(envFd, 0, SEEK_END);
        size = ftell(envFd);
        rewind(envFd);
        env = malloc(size);
        if (fread(env, 1, size, envFd) != size) {
            perror("Failed to read env file");
            doExit(1);
        }
        fclose(envFd);
    }

    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    creationFlags = CREATE_UNICODE_ENVIRONMENT;
    if (!wcscmp(argv[ArgAction], L"debug")) {
        mode = Debug;
    }

    switch (mode) {
    case Debug:
        creationFlags |= CREATE_SUSPENDED;
        break;
    default:
        break;
    }

    if (!CreateProcessW(0, argv[ArgCmdLine], 0, 0, FALSE, creationFlags, env, 0, &si, &pi)) {
        /* Only expected error: no such file or direcotry, i.e. executable not found */
        sendMsg("err:exec %d\n", GetLastError());
        doExit(1);
    }

    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    sendMsg("thread %d\n", pi.dwThreadId);
    sendMsg("pid %d\n", pi.dwProcessId);

    if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED)
        systemError("Wait for debugee failed, error %d\n");

    /* Don't close the process/thread handles, so that the kernel doesn't free
       the resources before ConsoleProcess is able to obtain handles to them
       - this would be a problem if the child process exits very quickly. */
    doExit(0);

    return 0;
}
