#pragma once

#include <QObject>
#include <QProcess>
#include <QUrl>

#include "downloader.h"
#include "filepicker.h"
#include "mixer.h"
#include "separator.h"

// The app's one job: take a song (a file, or a link to download it from),
// have it split into stems, and hand the stems to the mixer. Owns the
// dialogs, the download, the Demucs run, and installing Demucs when it's
// missing.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY busyChanged)
    Q_PROPERTY(bool splitting READ splitting NOTIFY busyChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY busyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString song READ song NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    // A song another launch asked to open while a job runs, waiting on the
    // user to stop the job or keep it.
    Q_PROPERTY(QString pendingSong READ pendingSong NOTIFY pendingChanged)
    Q_PROPERTY(bool sixStems READ sixStems WRITE setSixStems NOTIFY sixStemsChanged)
    Q_PROPERTY(QString stemsRoot READ stemsRoot CONSTANT)
    Q_PROPERTY(QObject *mixer READ mixerObject CONSTANT)

public:
    // Without a downloader, the backend makes its own that uses yt-dlp.
    explicit Backend(FilePicker *picker, Separator *separator, Downloader *downloader = nullptr,
                     QObject *parent = nullptr);
    ~Backend() override;

    bool engineReady() const { return !m_separator->engine().isEmpty(); }
    bool installing() const { return m_installer.state() != QProcess::NotRunning; }
    bool splitting() const { return m_separator->running(); }
    bool downloading() const { return m_downloader->running(); }
    bool busy() const { return installing() || splitting() || downloading(); }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString song() const { return m_song; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QString pendingSong() const;
    bool sixStems() const { return m_sixStems; }
    void setSixStems(bool six);
    QString stemsRoot() const { return m_separator->stemsRoot(); }
    Mixer *mixer() { return &m_mixer; }
    QObject *mixerObject() { return &m_mixer; }

    Q_INVOKABLE void open();
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void openPath(const QString &path);
    // Downloads the audio of a YouTube (or other) link, then splits it.
    Q_INVOKABLE void openLink(const QString &link);
    // Opens the link on the clipboard, or says there isn't one.
    Q_INVOKABLE void pasteLink();
    // The clipboard's text when it's a link, to start the link field with.
    Q_INVOKABLE QString clipboardLink() const;
    Q_INVOKABLE void cancel();
    // Opens a path or link from another launch (`stems open FILE` while this
    // window is open). During a job it waits in pendingSong for the user.
    Q_INVOKABLE void openOrAsk(const QString &target);
    // Stops the job and opens the pending song once it has stopped.
    Q_INVOKABLE void replaceWithPending();
    Q_INVOKABLE void keepCurrent();
    Q_INVOKABLE void installEngine();
    Q_INVOKABLE void exportMix();
    Q_INVOKABLE void showFolder();
    Q_INVOKABLE void dismissError() { setError({}); }

    // The command that installs Demucs with CPU-only PyTorch through uv.
    // torchaudio 2.9 dropped the audio writer Demucs uses, hence the pins.
    static QStringList installArguments();
    static QString findUv();

signals:
    void engineChanged();
    void busyChanged();
    void progressChanged();
    void errorChanged();
    void noticeChanged();
    void pendingChanged();
    void sixStemsChanged();

private:
    void setError(const QString &message);
    void setNotice(const QString &message);
    void setProgress(double fraction, const QString &status);
    void loadFolder(const QString &folder);
    void writeMix(const QUrl &url);
    void openTarget(const QString &target);

    FilePicker *m_picker;
    Separator *m_separator;
    Downloader *m_downloader;
    Downloader m_ownDownloader;
    Mixer m_mixer;
    QProcess m_installer;
    QString m_installLog;
    double m_progress = -1;
    QString m_status;
    QString m_song;
    QString m_error;
    QString m_notice;
    QString m_pending;
    bool m_replacing = false;
    bool m_sixStems = false;
};
