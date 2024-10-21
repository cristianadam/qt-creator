// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "lb.h"

#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QGroupBox>
#include <QTextEdit>
#include <QSpinBox>

using namespace Layouting;

void bindTest()
{
    QWidget *w = nullptr;
    QGroupBox *g = nullptr;
    QLabel *l = nullptr;

    Group {
        bindTo(&w),  // Works, as GroupInterface derives from WidgetInterface
        // bindTo(&l),  // Does (intentionally) not work, GroupInterface does not derive from LabelInterface
        bindTo(&g),
    };
}

Column direct()
{
    Binder<QString> content;

    return Column {
        Label {
            text(content)
        },
        TextEdit {
            onValueChanged(content)
        },
        Label {
            text(content)
        },
    };
}

Column transformed()
{
    Binder<int> spinBox;

    Binder<QString> spinboxAsText = spinBox.transformed<QString>([](int value) {
         return QString("World: %1").arg(value);
    });

    return Column {
        SpinBox {
            onValueChanged(spinBox),
        },
        TextEdit {
            text(spinboxAsText),
        },
    };
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Binder<int> spinBox;
    // Binder<int> spinBox2;

    // Binder<QString> spinboxAsText = spinBox.transformed<QString>([](int value) {
    //       // return QString("World: %1").arg(value);
    //       return QString("XX");
    // });

    Group {
        size(300, 200),
        title("HHHHHHH"),
        Form {
            "Hallo",
            Group {
                title("Title"),
                direct(),
                //transformed(),
            },
            // Group {
            //     title("Title"),
            //     Column {
            //         SpinBox {
            //             // value(spinBox2),
            //             onValueChanged(spinBox),
            //         },
            //         TextEdit {
            //             // text(spinboxAsText),
            //             // onValueChanged(content)
            //         },
            //     }
            // },
            // br,
            // "Col",
            // Column {
            //     Row { "1", "2", "3" },
            //     Row { "3", "4", "6" }
            // },
            // br,
            // "Grid",
            // Grid {
            //     Span { 2, QString("1111111") }, "3", br,
            //     "3", "4", "6", br,
            //     "4", empty, "6", br,
            //     hr, "4", "6"
            // },
            br,
            Column {
                Label {
                    text("Hi"),
                    size(30, 20)
                },
                Row {
                    // SpinBox {
                    //     bindAs(spinBox),
                    //     // onTextChanged([&](const QString &text) { textId->setText(text); })
                    // },
                    st,
                    PushButton {
                        text("Quit"),
                        onClicked(QApplication::quit, &app)
                    }
                }
            }
        }
    }.show();

    return app.exec();
}
