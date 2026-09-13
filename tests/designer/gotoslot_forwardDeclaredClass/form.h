// Copyright header

#pragma once

#include <QWidget>

namespace Ui {
class Form;
}

// The class is named here first, so this is where the front ends record it,
// and it is not where a declaration can be written into.
class Form;

class Form : public QWidget
{
    Q_OBJECT

public:
    explicit Form(QWidget *parent = 0);
    ~Form();

private:
    Ui::Form *ui;
};
