// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "vcsbase_global.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCompleter;
QT_END_NAMESPACE

namespace VcsBase {

struct SubmitFieldWidgetPrivate;

class VCSBASE_EXPORT SubmitFieldWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QStringList fields READ fields WRITE setFields DESIGNABLE true)
    Q_PROPERTY(bool hasBrowseButton READ hasBrowseButton WRITE setHasBrowseButton DESIGNABLE true)
    Q_PROPERTY(bool allowDuplicateFields READ allowDuplicateFields WRITE setAllowDuplicateFields DESIGNABLE true)

public:
    explicit SubmitFieldWidget(QWidget *parent = nullptr);
    ~SubmitFieldWidget() override;

    QStringList fields() const;
    void setFields(const QStringList&);

    bool hasBrowseButton() const;
    void setHasBrowseButton(bool d);

    // Allow several entries for fields ("reviewed-by: a", "reviewed-by: b")
    bool allowDuplicateFields() const;
    void setAllowDuplicateFields(bool);

    QCompleter *completer() const;
    void setCompleter(QCompleter *c);

    QString fieldValue(int pos) const;
    void setFieldValue(int pos, const QString &value);

    QString fieldValues() const;

signals:
    void browseButtonClicked(int pos, const QString &field);

private:
    void slotRemove(int pos);
    void slotComboIndexChanged(int pos, int comboIndex);

    void removeField(int index);
    bool comboIndexChange(int fieldNumber, int index);
    void createField(const QString &f);

    SubmitFieldWidgetPrivate *d;
};

} // namespace VcsBase
