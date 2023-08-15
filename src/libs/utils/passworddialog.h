// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "checkablemessagebox.h"

#include <QDialog>

#include <memory>
#include <optional>

namespace Utils {

class PasswordDialogPrivate;

class QTCREATOR_UTILS_EXPORT PasswordDialog : public QDialog
{
public:
    PasswordDialog(const QString &title, const QString &prompt, QWidget *parent = nullptr);
    virtual ~PasswordDialog();

    QString password() const;

    static std::optional<QString> getPassword(const QString &title,
                                              const QString &prompt,
                                              const CheckableDecider &decider,
                                              QWidget *parent = nullptr);

private:
    std::unique_ptr<PasswordDialogPrivate> d;
};

} // namespace Utils