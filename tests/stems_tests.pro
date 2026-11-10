QT += core gui quick quickcontrols2 testlib dbus multimedia
CONFIG += c++17 testcase
TARGET = stems_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/filepicker.h ../src/mixer.h ../src/separator.h ../src/theme.h ../src/translator.h
SOURCES += stems_tests.cpp ../src/backend.cpp ../src/mixer.cpp ../src/separator.cpp ../src/theme.cpp ../src/translator.cpp
