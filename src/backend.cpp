#include "backend.h"

#include <QDesktopServices>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

Backend::Backend(FilePicker *picker, Separator *separator, Downloader *downloader, QObject *parent)
    : QObject(parent), m_picker(picker), m_separator(separator),
      m_downloader(downloader ? downloader : &m_ownDownloader) {
    m_sixStems = QSettings().value("sixStems", false).toBool();

    connect(m_picker, &FilePicker::openSelected, this, &Backend::openUrl);
    connect(m_picker, &FilePicker::saveSelected, this, &Backend::writeMix);
    connect(m_picker, &FilePicker::failed, this, &Backend::setError);

    connect(m_separator, &Separator::progress, this, &Backend::setProgress);
    connect(m_separator, &Separator::finished, this, [this](const QString &folder) {
        emit busyChanged();
        loadFolder(folder);
    });
    connect(m_separator, &Separator::failed, this, [this](const QString &message) {
        // An empty message is a cancel the user asked for.
        m_song.clear();
        emit busyChanged();
        if (!message.isEmpty())
            setError(message);
    });

    // A download ends by opening what it saved, which splits it.
    connect(m_downloader, &Downloader::titled, this, [this](const QString &title) {
        m_song = title;
        emit busyChanged();
    });
    connect(m_downloader, &Downloader::progress, this, &Backend::setProgress);
    connect(m_downloader, &Downloader::finished, this, [this](const QString &path) {
        emit busyChanged();
        openPath(path);
    });
    connect(m_downloader, &Downloader::failed, this, [this](const QString &message) {
        m_song.clear();
        emit busyChanged();
        if (!message.isEmpty())
            setError(message);
    });

    connect(&m_mixer, &Mixer::loadFailed, this, &Backend::setError);
    connect(&m_mixer, &Mixer::saveFailed, this, &Backend::setError);

    // Each song comes back the way it was left: pitch, mutes, solos, volumes.
    m_mixer.setAutosave(true);
    connect(&m_mixer, &Mixer::loadedChanged, this, [this] {
        if (m_mixer.loaded() && m_mixer.restoreSettings())
            setNotice(tr("Restored this song's mix"));
    });
    connect(&m_mixer, &Mixer::audioFailed, this, &Backend::setError);

    m_installer.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_installer, &QProcess::readyRead, this, [this] {
        m_installLog += QString::fromUtf8(m_installer.readAll());
        const QStringList lines = m_installLog.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            setProgress(-1, tr("Installing Demucs: %1").arg(lines.last().trimmed().left(80)));
    });
    connect(&m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit busyChanged();
            setError(tr("uv could not be started to install Demucs."));
        }
    });
    connect(&m_installer, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_separator->setEngine(Separator::findEngine());
        emit busyChanged();
        emit engineChanged();
        if (status != QProcess::NormalExit || code != 0 || !engineReady()) {
            const QStringList lines = m_installLog.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
            setError(tr("Installing Demucs failed: %1").arg(lines.mid(qMax(0, lines.size() - 3)).join('\n')));
        } else {
            setNotice(tr("Demucs is installed. Open a song to split it."));
        }
    });
}

Backend::~Backend() {
    if (installing()) {
        m_installer.kill();
        m_installer.waitForFinished(2000);
    }
}

void Backend::setSixStems(bool six) {
    if (six == m_sixStems)
        return;
    m_sixStems = six;
    QSettings().setValue("sixStems", six);
    emit sixStemsChanged();
}

void Backend::setError(const QString &message) {
    if (message == m_error)
        return;
    m_error = message;
    emit errorChanged();
}

void Backend::setNotice(const QString &message) {
    m_notice = message;
    emit noticeChanged();
    if (!message.isEmpty())
        QTimer::singleShot(4000, this, [this, message] {
            if (m_notice == message)
                setNotice({});
        });
}

void Backend::setProgress(double fraction, const QString &status) {
    m_progress = fraction;
    m_status = status;
    emit progressChanged();
}

void Backend::open() {
    const QString folder = m_mixer.folder().isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
        : m_mixer.folder();
    m_picker->openAudio(folder);
}

void Backend::openUrl(const QUrl &url) {
    if (url.isLocalFile())
        openPath(url.toLocalFile());
    else if (url.scheme() == "http" || url.scheme() == "https")
        openLink(url.toString());
    else
        openPath(url.toString());
}

void Backend::openLink(const QString &link) {
    if (busy()) {
        setError(tr("Wait for the current job to finish, or press Esc to cancel it."));
        return;
    }
    if (!Downloader::isLink(link)) {
        setError(tr("%1 isn't a web link.").arg(link.trimmed().left(200)));
        return;
    }
    if (m_downloader->tool().isEmpty()) {
        setError(tr("Downloading from a link needs yt-dlp. Install it with: omarchy-pkg-add yt-dlp"));
        return;
    }
    // Without Demucs the download would only end in the same error.
    if (!engineReady()) {
        setError(tr("Demucs, the stem separation engine, isn't installed. Press I to install it."));
        return;
    }
    setError({});
    m_mixer.clear();
    m_song = QUrl(link.trimmed()).host();
    m_downloader->start(link);
    emit busyChanged();
}

QString Backend::clipboardLink() const {
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    return Downloader::isLink(text) ? text : QString();
}

void Backend::pasteLink() {
    const QString link = clipboardLink();
    if (link.isEmpty())
        setError(tr("The clipboard has no link in it. Copy a YouTube link first."));
    else
        openLink(link);
}

void Backend::openPath(const QString &path) {
    if (busy()) {
        setError(tr("Wait for the current job to finish, or press Esc to cancel it."));
        return;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        setError(tr("%1 doesn't exist.").arg(path));
        return;
    }
    setError({});

    // A stems folder, or one of the stems in it, opens straight in the mixer.
    if (info.isDir()) {
        if (Separator::isStemsFolder(path))
            loadFolder(path);
        else
            setError(tr("%1 has no stems in it.").arg(info.fileName()));
        return;
    }
    if (Separator::knownStems().contains(info.completeBaseName()) && Separator::isStemsFolder(info.absolutePath())) {
        loadFolder(info.absolutePath());
        return;
    }

    const QString folder = m_separator->folderFor(path, m_sixStems);
    if (Separator::isStemsFolder(folder)) {
        loadFolder(folder);
        return;
    }
    if (!engineReady()) {
        setError(tr("Demucs, the stem separation engine, isn't installed. Press I to install it."));
        return;
    }
    m_mixer.clear();
    m_song = info.completeBaseName();
    m_separator->start(path, m_sixStems);
    emit busyChanged();
}

void Backend::loadFolder(const QString &folder) {
    m_song.clear();
    emit busyChanged();
    m_mixer.load(Separator::stemFiles(folder), folder, QFileInfo(folder).fileName());
}

void Backend::cancel() {
    if (downloading())
        m_downloader->cancel();
    else if (splitting())
        m_separator->cancel();
    else if (installing())
        m_installer.kill();
}

QString Backend::findUv() {
    const QString found = QStandardPaths::findExecutable("uv");
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable("uv", {QDir::homePath() + "/.local/bin",
                                                 QDir::homePath() + "/.local/share/mise/shims",
                                                 QDir::homePath() + "/.cargo/bin"});
}

QStringList Backend::installArguments() {
    return {"tool", "install", "--force", "demucs", "--python", "3.12",
            "--with", "torch==2.8.*", "--with", "torchaudio==2.8.*", "--with", "numpy", "--with", "soundfile",
            "--index", "https://download.pytorch.org/whl/cpu", "--index-strategy", "unsafe-best-match"};
}

void Backend::installEngine() {
    if (busy() || engineReady())
        return;
    const QString uv = findUv();
    if (uv.isEmpty()) {
        setError(tr("Installing Demucs needs uv. Install it with: omarchy-pkg-add uv"));
        return;
    }
    setError({});
    m_installLog.clear();
    setProgress(-1, tr("Installing Demucs (about 1 GB to download)…"));
    m_installer.start(uv, installArguments());
    emit busyChanged();
}

void Backend::exportMix() {
    if (!m_mixer.loaded())
        return;
    QStringList audible;
    const auto gains = m_mixer.targetGains();
    const QStringList names = m_mixer.names();
    for (int i = 0; i < names.size(); ++i)
        if (gains[size_t(i)] > 0)
            audible << names[i];
    QString name = QStringLiteral("%1 - %2.wav").arg(m_mixer.title(), audible.join('+'));
    if (audible.size() == names.size())
        name = QStringLiteral("%1 - mix.wav").arg(m_mixer.title());
    if (m_mixer.cents() != 0)
        name.replace(".wav", QStringLiteral(" %1%2c.wav").arg(m_mixer.cents() > 0 ? "+" : "").arg(m_mixer.cents()));
    m_picker->saveMix(QUrl::fromLocalFile(QDir(m_mixer.folder()).filePath(name)));
}

void Backend::writeMix(const QUrl &url) {
    QString path = url.toLocalFile();
    if (!path.endsWith(".wav", Qt::CaseInsensitive))
        path += ".wav";
    const QString problem = m_mixer.exportMix(path);
    if (problem.isEmpty())
        setNotice(tr("Exported %1").arg(QFileInfo(path).fileName()));
    else
        setError(problem);
}

void Backend::showFolder() {
    const QString folder = m_mixer.folder().isEmpty() ? stemsRoot() : m_mixer.folder();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}
