#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <cstdio>

#include "backend.h"
#include "downloader.h"
#include "mixer.h"
#include "separator.h"

namespace {
const char *Usage = R"(Usage:
  stems [open] [FILE]                 Open the app, optionally with a song or stems folder
  stems check [--json]                Report whether Demucs and ffmpeg are installed
  stems split FILE [--six] [--json]   Split a song into stems under ~/Music/Stems
  stems split URL [--six] [--json]    Download a YouTube (or other) link's audio with
                                      yt-dlp into ~/Music/Stems/Downloads, then split it
  stems mix FOLDER -o OUT.wav [--saved] [--mute a,b] [--solo a,b] [--cents N] [--json]
                                      Render a mix of a stems folder to a WAV file;
                                      --saved starts from the mix saved in the app,
                                      --cents shifts the pitch (-100 to 100)
  stems skill install                 Install the agent skill to ~/.claude/skills/stems
)";

struct Options {
    QStringList positional;
    bool json = false;
    bool six = false;
    bool saved = false;
    int cents = 0;
    bool hasCents = false;
    QString output;
    QStringList mute, solo;
    QString error;
};

Options parse(const QStringList &arguments) {
    Options options;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &a = arguments[i];
        auto value = [&]() -> QString {
            if (i + 1 >= arguments.size()) {
                options.error = QStringLiteral("%1 needs a value").arg(a);
                return {};
            }
            return arguments[++i];
        };
        if (a == "--json") options.json = true;
        else if (a == "--six" || a == "--6") options.six = true;
        else if (a == "--saved") options.saved = true;
        else if (a == "--cents") {
            bool ok = false;
            options.cents = value().toInt(&ok);
            options.hasCents = true;
            if (!ok || options.cents < -100 || options.cents > 100)
                options.error = QStringLiteral("--cents takes a whole number from -100 to 100");
        }
        else if (a == "-o" || a == "--output") options.output = value();
        else if (a == "--mute") options.mute = value().split(',', Qt::SkipEmptyParts);
        else if (a == "--solo") options.solo = value().split(',', Qt::SkipEmptyParts);
        else if (a.startsWith("-")) options.error = QStringLiteral("unknown option %1").arg(a);
        else options.positional << a;
    }
    return options;
}

void out(const QString &text) {
    std::fputs(text.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
}

int fail(const Options &options, const QString &message) {
    if (options.json)
        out(QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", false}, {"error", message}}).toJson(QJsonDocument::Compact)));
    std::fprintf(stderr, "stems: %s\n", message.toUtf8().constData());
    return 1;
}

int succeed(const Options &options, QJsonObject result, const QString &text) {
    result.insert("ok", true);
    if (options.json)
        out(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    else if (!text.isEmpty())
        out(text);
    return 0;
}

QJsonArray stemsJson(const QString &folder) {
    QJsonArray stems;
    for (const auto &file : Separator::stemFiles(folder))
        stems.append(QJsonObject{{"name", QFileInfo(file).completeBaseName()}, {"path", file}});
    return stems;
}

int check(const Options &options) {
    const QString demucs = Separator::findEngine();
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    const QString uv = Backend::findUv();
    const QString ytdlp = Downloader::findTool();
    QJsonObject result{{"demucs", demucs}, {"ffmpeg", ffmpeg}, {"uv", uv}, {"ytdlp", ytdlp},
                       {"stemsRoot", Separator::defaultRoot()}};
    if (demucs.isEmpty() || ffmpeg.isEmpty()) {
        QString message = demucs.isEmpty() ? QStringLiteral("Demucs is not installed; install it with: uv %1")
                                                 .arg(Backend::installArguments().join(' '))
                                           : QStringLiteral("ffmpeg is not installed");
        if (options.json) {
            result.insert("ok", false);
            result.insert("error", message);
            out(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }
        std::fprintf(stderr, "stems: %s\n", message.toUtf8().constData());
        return 1;
    }
    return succeed(options, result, QStringLiteral("demucs: %1\nffmpeg: %2\nyt-dlp: %3\nstems go in: %4")
                                        .arg(demucs, ffmpeg, ytdlp.isEmpty() ? "not installed (links can't be split)" : ytdlp,
                                             Separator::defaultRoot()));
}

// Prints a job's progress to stderr, once per whole percent or new status.
auto progressPrinter() {
    return [lastShown = -2, lastStatus = QString()](double fraction, const QString &status) mutable {
        const int percent = fraction < 0 ? -1 : int(fraction * 100);
        if (percent == lastShown && status == lastStatus)
            return;
        lastShown = percent;
        lastStatus = status;
        if (percent < 0)
            std::fprintf(stderr, "%s\n", status.toUtf8().constData());
        else
            std::fprintf(stderr, "%s %d%%\n", status.toUtf8().constData(), percent);
    };
}

int split(const Options &options) {
    if (options.positional.size() != 1)
        return fail(options, "split takes one audio file or link");
    QString input = options.positional.first();
    QString downloaded;

    // A link is downloaded first; what it saves is split like any file.
    if (Downloader::isLink(input)) {
        if (Separator::findEngine().isEmpty())
            return fail(options, "Demucs is not installed; run stems check");
        Downloader downloader;
        QEventLoop loop;
        QString error;
        QObject::connect(&downloader, &Downloader::progress, progressPrinter());
        QObject::connect(&downloader, &Downloader::finished, [&](const QString &path) { downloaded = path; loop.quit(); });
        QObject::connect(&downloader, &Downloader::failed, [&](const QString &e) { error = e; loop.quit(); });
        downloader.start(input);
        loop.exec();
        if (downloaded.isEmpty())
            return fail(options, error.isEmpty() ? QStringLiteral("cancelled") : error);
        input = downloaded;
    }
    if (!QFileInfo(input).isFile())
        return fail(options, QStringLiteral("%1 is not a file").arg(input));

    Separator separator;
    QEventLoop loop;
    QString folder, error;
    QObject::connect(&separator, &Separator::progress, progressPrinter());
    QObject::connect(&separator, &Separator::finished, [&](const QString &f) { folder = f; loop.quit(); });
    QObject::connect(&separator, &Separator::failed, [&](const QString &e) { error = e; loop.quit(); });
    separator.start(input, options.six);
    loop.exec();
    if (folder.isEmpty())
        return fail(options, error.isEmpty() ? QStringLiteral("cancelled") : error);
    QStringList lines{folder};
    lines << Separator::stemFiles(folder);
    QJsonObject result{{"folder", folder}, {"stems", stemsJson(folder)}};
    if (!downloaded.isEmpty())
        result.insert("download", downloaded);
    return succeed(options, result, lines.join('\n'));
}

int mix(const Options &options) {
    if (options.positional.size() != 1)
        return fail(options, "mix takes one stems folder");
    if (options.output.isEmpty())
        return fail(options, "mix needs -o OUT.wav");
    const QString folder = options.positional.first();
    if (!Separator::isStemsFolder(folder))
        return fail(options, QStringLiteral("%1 has no stems in it").arg(folder));

    Mixer mixer;
    QEventLoop loop;
    QString error;
    QObject::connect(&mixer, &Mixer::loadedChanged, &loop, [&] { if (mixer.loaded()) loop.quit(); });
    QObject::connect(&mixer, &Mixer::loadFailed, &loop, [&](const QString &e) { error = e; loop.quit(); });
    mixer.load(Separator::stemFiles(folder), folder, QFileInfo(folder).fileName());
    if (!mixer.loaded() && error.isEmpty())
        loop.exec();
    if (!error.isEmpty())
        return fail(options, error);

    if (options.saved && !mixer.restoreSettings())
        return fail(options, QStringLiteral("%1 has no saved mix").arg(folder));
    if (options.hasCents)
        mixer.setCents(options.cents);

    const QStringList names = mixer.names();
    for (const auto &name : options.mute + options.solo)
        if (!names.contains(name))
            return fail(options, QStringLiteral("no stem called %1 (there are: %2)").arg(name, names.join(", ")));
    // Flags set a stem's state outright, on top of a saved mix if asked for.
    for (int i = 0; i < names.size(); ++i) {
        const QModelIndex row = mixer.index(i);
        if (options.mute.contains(names[i]) && !mixer.data(row, Mixer::MutedRole).toBool()) mixer.toggleMute(i);
        if (options.solo.contains(names[i]) && !mixer.data(row, Mixer::SoloedRole).toBool()) mixer.toggleSolo(i);
    }
    QString output = options.output;
    if (!output.endsWith(".wav", Qt::CaseInsensitive))
        output += ".wav";
    const QString problem = mixer.exportMix(output);
    if (!problem.isEmpty())
        return fail(options, problem);
    return succeed(options, {{"output", QFileInfo(output).absoluteFilePath()}}, QFileInfo(output).absoluteFilePath());
}

int installSkill(const Options &options) {
    const QString directory = QDir::homePath() + "/.claude/skills/stems";
    QFile source(":/skill/SKILL.md");
    if (!source.open(QIODevice::ReadOnly) || !QDir().mkpath(directory))
        return fail(options, QStringLiteral("could not write %1").arg(directory));
    QSaveFile target(directory + "/SKILL.md");
    if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit())
        return fail(options, QStringLiteral("could not write %1/SKILL.md").arg(directory));
    return succeed(options, {{"path", directory + "/SKILL.md"}}, directory + "/SKILL.md");
}
}

bool Cli::isCommand(const QStringList &arguments) {
    if (arguments.size() < 2)
        return false;
    static const QStringList commands{"check", "split", "mix", "skill", "help", "--help", "-h", "--version"};
    return commands.contains(arguments[1]);
}

int Cli::run(const QStringList &arguments) {
    const QString command = arguments.value(1);
    const Options options = parse(arguments.mid(command == "skill" ? 3 : 2));
    if (command == "help" || command == "--help" || command == "-h") {
        std::fputs(Usage, stdout);
        return 0;
    }
    if (command == "--version") {
        out(QCoreApplication::applicationVersion());
        return 0;
    }
    if (!options.error.isEmpty())
        return fail(options, options.error);
    if (command == "check") return check(options);
    if (command == "split") return split(options);
    if (command == "mix") return mix(options);
    if (command == "skill" && arguments.value(2) == "install") return installSkill(options);
    std::fputs(Usage, stderr);
    return 1;
}
