QT += core gui qml quick quickcontrols2 dbus multimedia

CONFIG += c++17 release
TARGET = stems
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/filepicker.h \
    src/mixer.h \
    src/portalfilepicker.h \
    src/separator.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/mixer.cpp \
    src/portalfilepicker.cpp \
    src/separator.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
