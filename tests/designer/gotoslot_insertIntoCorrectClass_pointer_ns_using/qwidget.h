// Copyright header

// What these tests need of QWidget, so that the class a form belongs to reads
// as a class rather than as a declaration of an unknown type. A front end
// that resolves names cannot read a constructor whose parameter type nothing
// declares, and every form's constructor takes one of these.

#pragma once

class QWidget
{
public:
    explicit QWidget(QWidget *parent = 0);
    virtual ~QWidget();
};
