TEMPLATE = lib
CONFIG -= qt
SOURCES = multi-target-project-dyn.cpp
LIBS += -L$$OUT_PWD/../lib -lmulti-target-project-lib
