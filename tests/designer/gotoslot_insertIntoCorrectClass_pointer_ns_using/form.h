// Copyright header

#pragma once

#include "qwidget.h"

namespace N {
namespace Ui {
class Form;
}
}

using namespace N;

class Form : public QWidget
{
    Q_OBJECT

public:
    explicit Form(QWidget *parent = 0);
    ~Form();

private:
    Ui::Form *ui;
};
