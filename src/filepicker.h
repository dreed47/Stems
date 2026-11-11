#pragma once

#include <QObject>
#include <QUrl>

// Open and save dialogs behind an interface, so tests can inject a fake and
// the real one can go through xdg-desktop-portal. Follows Omacut (MIT, David
// Heinemeier Hansson).
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openAudio(const QString &folder) = 0;
    virtual void saveMix(const QUrl &suggestedUrl) = 0;
    virtual void chooseFolder(const QString &folder) = 0;

signals:
    void openSelected(const QUrl &url);
    void saveSelected(const QUrl &url);
    void folderSelected(const QUrl &url);
    void failed(const QString &message);
};
