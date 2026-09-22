// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include <utils/filepath.h>

namespace Utils { class FilePath; }

namespace CppEditor {

class CPPEDITOR_EXPORT ProjectFile
{
public:
    enum Kind {
        Unclassified,
        Unsupported,
        AmbiguousHeader,
        CHeader,
        CSource,
        CXXHeader,
        CXXSource,
        ObjCHeader,
        ObjCSource,
        ObjCXXHeader,
        ObjCXXSource,
        CudaSource,
        OpenCLSource,
    };

    ProjectFile() = default;
    ProjectFile(const Utils::FilePath &filePath, Kind kind, bool active = true);

    static Kind classifyByMimeType(const QString &mt);
    static Kind classify(const Utils::FilePath &filePath);

    static Kind sourceForHeaderKind(Kind kind);
    static Kind sourceKind(Kind kind);

    // Whether a front end reads this file as C++ at all -- which is what a
    // consumer that used to ask whether the built-in code model had a
    // document for it wants to know, the cxx front end's index producing no
    // such document. Everything the code model handles counts: C,
    // Objective-C(++), CUDA and OpenCL as much as C++.
    static bool isCppFile(Kind kind);
    static bool isCppFile(const Utils::FilePath &filePath);

    static bool isSource(Kind kind);
    static bool isHeader(Kind kind);
    static bool isHeader(const Utils::FilePath &fp);
    static bool isC(Kind kind);
    static bool isCxx(Kind kind);
    static bool isAmbiguousHeader(QStringView filePath);
    static bool isAmbiguousHeader(const Utils::FilePath &filePath);
    static bool isObjC(const Utils::FilePath &filePath);
    static bool isObjC(Kind kind);

    bool isHeader() const;
    bool isSource() const;

    bool isC() const;
    bool isCxx() const;

    bool operator==(const ProjectFile &other) const;
    friend QDebug operator<<(QDebug stream, const CppEditor::ProjectFile &projectFile);

public:
    Utils::FilePath path;
    Kind kind = Unclassified;
    bool active = true;
};

using ProjectFiles = QList<ProjectFile>;

const char *projectFileKindToText(ProjectFile::Kind kind);

} // CppEditor
