#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

// Fetches the audio of a YouTube video (or anything else yt-dlp can read)
// into ~/Music/Stems/Downloads, so it can be split like any other song.
class Downloader : public QObject {
    Q_OBJECT

public:
    explicit Downloader(const QString &folder = {}, const QString &tool = {}, QObject *parent = nullptr);
    ~Downloader() override;

    QString folder() const { return m_folder; }
    QString tool() const { return m_tool; }
    bool running() const { return m_process.state() != QProcess::NotRunning; }

    void start(const QString &url);
    void cancel();

    static QString findTool();
    static QString defaultFolder();
    // An http or https address with a host: what a pasted link has to be.
    static bool isLink(const QString &text);
    // The fraction in one line of yt-dlp's "[download]  42.0% of …", or -1.
    static double parseProgress(const QString &line);
    static QStringList arguments(const QString &folder, const QString &url);

signals:
    void titled(const QString &title);
    void progress(double fraction, const QString &status);
    void finished(const QString &path);
    void failed(const QString &message);

private:
    void handleOutput();
    void handleLine(const QString &line);
    void handleFinished(int code, QProcess::ExitStatus status);

    QString m_folder;
    QString m_tool;
    QString m_title;
    QString m_file;
    QStringList m_errors;
    QByteArray m_pending;
    QProcess m_process;
    bool m_cancelled = false;
};
