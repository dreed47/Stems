#include "downloader.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "separator.h"

namespace {
// yt-dlp prints these, marked so they can't be mistaken for its other output.
const QString TitleMark = QStringLiteral("stems-title:");
const QString FileMark = QStringLiteral("stems-file:");
}

Downloader::Downloader(const QString &folder, const QString &tool, QObject *parent)
    : QObject(parent),
      m_folder(folder.isEmpty() ? defaultFolder() : folder),
      m_tool(tool.isEmpty() ? findTool() : tool) {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &Downloader::handleOutput);
    connect(&m_process, &QProcess::finished, this, &Downloader::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit failed(tr("yt-dlp could not be started (%1).").arg(m_tool));
    });
}

Downloader::~Downloader() {
    if (running()) {
        m_cancelled = true;
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

// The Arch package lands in /usr/bin; a uv or pip install in ~/.local/bin,
// which a session started from the launcher may not have on its PATH.
QString Downloader::findTool() {
    const QString found = QStandardPaths::findExecutable("yt-dlp");
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable("yt-dlp", {QDir::homePath() + "/.local/bin"});
}

QString Downloader::defaultFolder() {
    return Separator::defaultRoot() + "/Downloads";
}

bool Downloader::isLink(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.contains(QRegularExpression("\\s")))
        return false;
    const QUrl url(trimmed, QUrl::StrictMode);
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty();
}

double Downloader::parseProgress(const QString &line) {
    static const QRegularExpression percent(R"(^\[download\]\s+(\d+(?:\.\d+)?)%)");
    const auto match = percent.match(line.trimmed());
    return match.hasMatch() ? qBound(0.0, match.captured(1).toDouble() / 100.0, 1.0) : -1;
}

QStringList Downloader::arguments(const QString &folder, const QString &url) {
    return {"--newline", "--progress", "--no-playlist", "--no-warnings",
            // The best audio stream as it is, with no re-encoding: Demucs
            // reads it through ffmpeg, and every conversion loses a little.
            "-f", "bestaudio/best", "-x",
            "--trim-filenames", "150",
            "-o", QDir(folder).filePath("%(title)s.%(ext)s"),
            "--print", "before_dl:" + TitleMark + "%(title)s",
            "--print", "after_move:" + FileMark + "%(filepath)s",
            "--no-simulate",
            "--", url.trimmed()};
}

void Downloader::start(const QString &url) {
    if (running())
        return;
    auto fail = [this](const QString &message) {
        QMetaObject::invokeMethod(this, [this, message] { emit failed(message); }, Qt::QueuedConnection);
    };
    if (!isLink(url))
        return fail(tr("%1 isn't a web link.").arg(url.trimmed()));
    if (m_tool.isEmpty())
        return fail(tr("Downloading from a link needs yt-dlp. Install it with: omarchy-pkg-add yt-dlp"));
    if (!QDir().mkpath(m_folder))
        return fail(tr("Could not create %1.").arg(m_folder));

    m_title.clear();
    m_file.clear();
    m_errors.clear();
    m_pending.clear();
    m_cancelled = false;
    emit progress(-1, tr("Looking up the link…"));
    m_process.start(m_tool, arguments(m_folder, url));
}

void Downloader::cancel() {
    if (!running())
        return;
    m_cancelled = true;
    m_process.kill();
}

void Downloader::handleOutput() {
    m_pending += m_process.readAll();
    qsizetype end;
    while ((end = m_pending.indexOf('\n')) >= 0) {
        handleLine(QString::fromUtf8(m_pending.left(end)).trimmed());
        m_pending.remove(0, end + 1);
    }
}

void Downloader::handleLine(const QString &line) {
    if (line.startsWith(TitleMark)) {
        m_title = line.mid(TitleMark.size());
        emit titled(m_title);
        emit progress(0, tr("Downloading…"));
    } else if (line.startsWith(FileMark)) {
        m_file = line.mid(FileMark.size());
    } else if (line.startsWith("ERROR:")) {
        m_errors << line.mid(6).trimmed();
    } else if (line.startsWith("[ExtractAudio]")) {
        emit progress(1, tr("Saving the audio…"));
    } else {
        const double fraction = parseProgress(line);
        if (fraction >= 0)
            emit progress(fraction, tr("Downloading…"));
    }
}

void Downloader::handleFinished(int code, QProcess::ExitStatus status) {
    if (!m_pending.isEmpty()) {
        handleLine(QString::fromUtf8(m_pending).trimmed());
        m_pending.clear();
    }
    if (m_cancelled) {
        emit failed({});
        return;
    }
    if (status != QProcess::NormalExit || code != 0 || m_file.isEmpty() || !QFileInfo(m_file).isFile()) {
        emit failed(m_errors.isEmpty() ? tr("yt-dlp stopped without saving any audio.")
                                       : tr("Downloading failed: %1").arg(m_errors.last()));
        return;
    }
    emit finished(m_file);
}
