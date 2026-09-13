// Copyright header

#pragma once

#include <QWidget>

namespace Ui {
class Form;
}

class Form : public QWidget
{
    Q_OBJECT

public:
    explicit Form(QWidget *parent = 0);
    ~Form();

private slots:
    void onPushButtonClicked();

private:
    Ui::Form *ui;
};
