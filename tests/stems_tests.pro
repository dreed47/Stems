QT += core gui quick quickcontrols2 testlib dbus multimedia network
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += rubberband
TARGET = stems_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/downloader.h ../src/filepicker.h ../src/mixer.h ../src/pitchshifter.h ../src/separator.h ../src/tempo.h ../src/structure.h ../src/singleinstance.h ../src/theme.h ../src/translator.h
SOURCES += stems_tests.cpp ../src/backend.cpp ../src/downloader.cpp ../src/mixer.cpp ../src/pitchshifter.cpp ../src/separator.cpp ../src/tempo.cpp ../src/structure.cpp ../src/singleinstance.cpp ../src/theme.cpp ../src/translator.cpp
