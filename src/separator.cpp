#include "separator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

Separator::Separator(const QString &stemsRoot, const QString &demucs, QObject *parent)
    : QObject(parent),
      m_root(stemsRoot.isEmpty() ? defaultRoot() : stemsRoot),
      m_engine(demucs.isEmpty() ? findEngine() : demucs) {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &Separator::handleOutput);
    connect(&m_process, &QProcess::finished, this, &Separator::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_work.reset();
            emit failed(tr("Demucs could not be started (%1).").arg(m_engine));
        }
    });
}

Separator::~Separator() {
    if (running()) {
        m_cancelled = true;
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

// uv installs tools into ~/.local/bin, which a session started from the
// launcher doesn't always have on its PATH, so look there too.
QString Separator::findEngine() {
    const QString found = QStandardPaths::findExecutable("demucs");
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable("demucs", {QDir::homePath() + "/.local/bin"});
}

QString Separator::defaultRoot() {
    const QString chosen = QSettings().value("stemsRoot").toString();
    return chosen.isEmpty() ? builtinRoot() : chosen;
}

QString Separator::builtinRoot() {
    QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (music.isEmpty() || music == QDir::homePath())
        music = QDir::homePath() + "/Music";
    return music + "/Stems";
}

const QStringList &Separator::knownStems() {
    static const QStringList names{"vocals", "drums", "bass", "guitar", "piano", "other"};
    return names;
}

QStringList Separator::stemFiles(const QString &folder) {
    QStringList files;
    const QDir dir(folder);
    for (const auto &name : knownStems())
        for (const auto &extension : {"wav", "flac", "mp3"}) {
            const QString path = dir.filePath(name + "." + extension);
            if (QFileInfo(path).isFile()) {
                files << path;
                break;
            }
        }
    return files;
}

QString Separator::folderFor(const QString &input, bool sixStems) const {
    QString name = QFileInfo(input).completeBaseName().trimmed();
    name.replace('/', '_');
    if (name.isEmpty() || name.startsWith('.'))
        name = "song" + name;
    if (sixStems)
        name += " (6 stems)";
    return QDir(m_root).filePath(name);
}

double Separator::parseProgress(const QString &chunk, int *bars, int *barIndex, double *lastPercent) {
    static const QRegularExpression bag(R"(bag of (\d+) models?)");
    static const QRegularExpression bar(R"((\d{1,3})%\|)");
    double fraction = -1;
    for (const auto &piece : chunk.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)) {
        const auto bagMatch = bag.match(piece);
        if (bagMatch.hasMatch())
            *bars = qMax(1, bagMatch.captured(1).toInt());
        const auto barMatch = bar.match(piece);
        if (!barMatch.hasMatch())
            continue;
        const double percent = qBound(0, barMatch.captured(1).toInt(), 100);
        // A bar starting over means the next model in the bag has begun.
        if (percent + 5 < *lastPercent)
            ++*barIndex;
        *lastPercent = percent;
        const int index = qMin(*barIndex, *bars - 1);
        fraction = (index + percent / 100.0) / *bars;
    }
    return fraction;
}

bool Separator::replaceStems(const QString &source, const QString &target, const QString &backup) {
    if (!QDir().mkpath(backup))
        return false;
    QStringList movedOut;
    for (const auto &file : stemFiles(target)) {
        if (!QDir().rename(file, QDir(backup).filePath(QFileInfo(file).fileName())))
            break;
        movedOut << file;
    }
    bool ok = movedOut.size() == stemFiles(backup).size() && stemFiles(target).isEmpty();
    QStringList movedIn;
    for (const auto &file : ok ? stemFiles(source) : QStringList()) {
        const QString to = QDir(target).filePath(QFileInfo(file).fileName());
        if (!QDir().rename(file, to)) {
            ok = false;
            break;
        }
        movedIn << to;
    }
    if (ok)
        return true;
    for (const auto &file : movedIn)
        QFile::remove(file);
    for (const auto &file : movedOut)
        QDir().rename(QDir(backup).filePath(QFileInfo(file).fileName()), file);
    return false;
}

void Separator::start(const QString &input, bool sixStems, bool replace) {
    if (running())
        return;
    const QString target = folderFor(input, sixStems);
    if (isStemsFolder(target) && !replace) {
        QMetaObject::invokeMethod(this, [this, target] { emit finished(target); }, Qt::QueuedConnection);
        return;
    }
    if (m_engine.isEmpty()) {
        QMetaObject::invokeMethod(this, [this] { emit failed(tr("Demucs, the stem separation engine, isn't installed.")); },
                                  Qt::QueuedConnection);
        return;
    }
    if (!QDir().mkpath(m_root)) {
        QMetaObject::invokeMethod(this, [this] { emit failed(tr("Could not create %1.").arg(m_root)); },
                                  Qt::QueuedConnection);
        return;
    }

    // Demucs writes into a hidden folder beside the final one, which is
    // renamed into place only once every stem is there, so a cancelled or
    // failed split never leaves half a song behind.
    m_work = std::make_unique<QTemporaryDir>(QDir(m_root).filePath(".split-XXXXXX"));
    if (!m_work->isValid()) {
        m_work.reset();
        QMetaObject::invokeMethod(this, [this] { emit failed(tr("Could not write to %1.").arg(m_root)); },
                                  Qt::QueuedConnection);
        return;
    }

    m_target = target;
    m_replace = replace;
    m_model = model(sixStems);
    m_log.clear();
    m_cancelled = false;
    m_separating = false;
    m_bars = 1;
    m_barIndex = 0;
    m_lastPercent = 0;
    emit progress(-1, tr("Loading the model…"));
    m_process.start(m_engine, {"-d", "cpu", "-n", m_model, "-o", m_work->path(),
                               "--filename", "{stem}.{ext}", QFileInfo(input).absoluteFilePath()});
}

void Separator::cancel() {
    if (!running())
        return;
    m_cancelled = true;
    m_process.kill();
}

void Separator::handleOutput() {
    const QString chunk = QString::fromUtf8(m_process.readAll());
    m_log += chunk;
    if (m_log.size() > 20000)
        m_log = m_log.right(10000);

    if (!m_separating) {
        // Bars before this line belong to the model download, not the split.
        const int at = chunk.indexOf("Separating track");
        if (at < 0) {
            if (chunk.contains("Downloading") || chunk.contains("%|"))
                emit progress(-1, tr("Downloading the model…"));
            return;
        }
        m_separating = true;
        const double fraction = parseProgress(chunk.mid(at), &m_bars, &m_barIndex, &m_lastPercent);
        emit progress(qMax(0.0, fraction), tr("Splitting…"));
        return;
    }
    const double fraction = parseProgress(chunk, &m_bars, &m_barIndex, &m_lastPercent);
    if (fraction >= 0)
        emit progress(fraction, tr("Splitting…"));
}

void Separator::handleFinished(int code, QProcess::ExitStatus status) {
    handleOutput();
    const QString source = QDir(m_work ? m_work->path() : QString()).filePath(m_model);
    if (m_cancelled) {
        m_work.reset();
        emit failed({});
        return;
    }
    if (status != QProcess::NormalExit || code != 0 || !isStemsFolder(source)) {
        m_work.reset();
        // The last lines Demucs printed say what went wrong.
        QStringList lines = m_log.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
        lines.removeIf([](const QString &line) { return line.contains("%|") || line.contains("Warning"); });
        const QString detail = lines.mid(qMax(0, lines.size() - 3)).join('\n').trimmed();
        emit failed(detail.isEmpty() ? tr("Demucs stopped without writing any stems.")
                                     : tr("Demucs failed: %1").arg(detail));
        return;
    }
    const bool moved = m_replace && isStemsFolder(m_target)
        ? replaceStems(source, m_target, QDir(m_work->path()).filePath("old"))
        : !QFileInfo::exists(m_target) && QDir().rename(source, m_target);
    if (!moved) {
        // Old stems that couldn't be put back must outlive the work folder.
        const QString backup = QDir(m_work->path()).filePath("old");
        if (!stemFiles(backup).isEmpty()) {
            m_work->setAutoRemove(false);
            m_work.reset();
            emit failed(tr("Could not move the stems into %1. The old stems are in %2.").arg(m_target, backup));
            return;
        }
        m_work.reset();
        emit failed(tr("Could not move the stems into %1.").arg(m_target));
        return;
    }
    m_work.reset();
    emit progress(1, tr("Done"));
    emit finished(m_target);
}
