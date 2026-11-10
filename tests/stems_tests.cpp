#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "backend.h"
#include "downloader.h"
#include "mixer.h"
#include "pitchshifter.h"
#include "separator.h"
#include "singleinstance.h"
#include "theme.h"
#include "translator.h"

class StemsTests : public QObject {
    Q_OBJECT

private slots:
    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void separatorNamesFolders()
    {
        Separator separator("/music/Stems", "/bin/false");
        QCOMPARE(separator.folderFor("/x/My Song.mp3", false), QString("/music/Stems/My Song"));
        QCOMPARE(separator.folderFor("/x/My Song.mp3", true), QString("/music/Stems/My Song (6 stems)"));
        QCOMPARE(separator.folderFor("/x/.hidden.mp3", false), QString("/music/Stems/song.hidden"));
    }

    void separatorListsStemsInOrder()
    {
        QTemporaryDir dir;
        for (const char *name : {"other.wav", "vocals.wav", "bass.flac", "notes.txt"})
            write(dir.filePath(name), "x");
        const QStringList files = Separator::stemFiles(dir.path());
        QCOMPARE(files, QStringList({dir.filePath("vocals.wav"), dir.filePath("bass.flac"), dir.filePath("other.wav")}));
        QVERIFY(Separator::isStemsFolder(dir.path()));
    }

    void separatorParsesProgress()
    {
        int bars = 1, index = 0;
        double last = 0;
        QCOMPARE(Separator::parseProgress("Selected model is a bag of 4 models.\n", &bars, &index, &last), -1.0);
        QCOMPARE(bars, 4);
        QCOMPARE(Separator::parseProgress(" 50%|#####     | 5/10 [..]\r", &bars, &index, &last), 0.125);
        Separator::parseProgress("100%|##########| 10/10\r", &bars, &index, &last);
        // The next model's bar starts over at zero.
        QCOMPARE(Separator::parseProgress("  0%|          | 0/10\r 50%|#####", &bars, &index, &last), 0.375);
    }

    void separatorSplitsWithDemucs()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath("Tune.mp3");
        write(song, "original");
        // A stand-in for Demucs that writes stems where it's told to.
        const QString demucs = script(dir, "demucs", R"(
out=""; model=""
while [ $# -gt 1 ]; do case "$1" in -o) out="$2"; shift;; -n) model="$2"; shift;; esac; shift; done
echo "Separating track $1"
printf ' 50%%|#####\r'
mkdir -p "$out/$model" && for s in vocals drums bass other; do echo $s > "$out/$model/$s.wav"; done
)");
        Separator separator(dir.filePath("Stems"), demucs);
        QSignalSpy finished(&separator, &Separator::finished);
        QSignalSpy progress(&separator, &Separator::progress);
        separator.start(song, false);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toString(), dir.filePath("Stems/Tune"));
        QCOMPARE(Separator::stemFiles(dir.filePath("Stems/Tune")).size(), 4);
        QVERIFY(!progress.isEmpty());
        // Nothing half-done is left behind, and the original is untouched.
        QCOMPARE(QDir(dir.filePath("Stems")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
                 QStringList{"Tune"});
        QFile original(song);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));

        // A second split of the same song finishes at once, without Demucs.
        separator.setEngine("/nonexistent");
        separator.start(song, false);
        QVERIFY(finished.wait(1000));
    }

    void separatorReportsDemucsErrors()
    {
        QTemporaryDir dir;
        write(dir.filePath("Tune.mp3"), "x");
        const QString demucs = script(dir, "demucs", "echo 'RuntimeError: out of memory' >&2; exit 1\n");
        Separator separator(dir.filePath("Stems"), demucs);
        QSignalSpy failed(&separator, &Separator::failed);
        separator.start(dir.filePath("Tune.mp3"), false);
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().contains("out of memory"));
        QVERIFY(!QFileInfo::exists(dir.filePath("Stems/Tune")));
    }

    void separatorCancels()
    {
        QTemporaryDir dir;
        write(dir.filePath("Tune.mp3"), "x");
        Separator separator(dir.filePath("Stems"), script(dir, "demucs", "sleep 30\n"));
        QSignalSpy failed(&separator, &Separator::failed);
        separator.start(dir.filePath("Tune.mp3"), false);
        QVERIFY(separator.running());
        separator.cancel();
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().isEmpty());
        QCOMPARE(QDir(dir.filePath("Stems")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size(), 0);
    }

    void mixerMutesSolosAndUndoes()
    {
        QTemporaryDir dir;
        Mixer mixer;
        QVERIFY(loadTones(dir, mixer));
        QCOMPARE(mixer.rowCount(), 3);
        QCOMPARE(mixer.names(), QStringList({"vocals", "drums", "bass"}));
        QCOMPARE(mixer.duration(), 1.0);
        QCOMPARE(mixer.targetGains(), std::vector<float>({1, 1, 1}));

        mixer.toggleMute(0);
        QCOMPARE(mixer.targetGains(), std::vector<float>({0, 1, 1}));
        mixer.toggleSolo(2);
        QCOMPARE(mixer.targetGains(), std::vector<float>({0, 0, 1}));
        // Solo wins over mute.
        mixer.toggleSolo(0);
        QCOMPARE(mixer.targetGains(), std::vector<float>({1, 0, 1}));
        mixer.setVolume(2, 0.5);
        QCOMPARE(mixer.targetGains(), std::vector<float>({1, 0, 0.5}));

        mixer.undo();
        mixer.undo();
        QCOMPARE(mixer.targetGains(), std::vector<float>({0, 0, 1}));
        mixer.redo();
        QCOMPARE(mixer.targetGains(), std::vector<float>({1, 0, 1}));
        mixer.resetMix();
        QCOMPARE(mixer.targetGains(), std::vector<float>({1, 1, 1}));
        QVERIFY(mixer.canUndo());
        QVERIFY(!mixer.canRedo());
    }

    void mixerExportsWhatIsAudible()
    {
        QTemporaryDir dir;
        Mixer mixer;
        QVERIFY(loadTones(dir, mixer));
        for (int i = 0; i < 3; ++i)
            mixer.toggleMute(i);
        QVERIFY(!mixer.exportMix(dir.filePath("silent.wav")).isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath("silent.wav")));

        mixer.toggleMute(1);
        const QString out = dir.filePath("drums only.wav");
        QCOMPARE(mixer.exportMix(out), QString());
        QFile file(out);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray wav = file.readAll();
        QCOMPARE(wav.left(4), QByteArray("RIFF"));
        QCOMPARE(wav.size(), 44 + Mixer::SampleRate * 4);

        // Only the drums tone made it in: compare with the drums stem itself.
        Mixer drums;
        QVERIFY(loadTones(dir, drums, true));
        drums.toggleSolo(1);
        QCOMPARE(drums.exportMix(dir.filePath("again.wav")), QString());
        QFile again(dir.filePath("again.wav"));
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), wav);
    }

    void backendOpensStemsFolders()
    {
        QTemporaryDir dir;
        QVERIFY(makeTones(dir));
        FakePicker picker;
        Separator separator(dir.filePath("Stems"), "/nonexistent-demucs");
        Backend backend(&picker, &separator);
        backend.openPath(dir.filePath("song/drums.wav"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.mixer()->loaded(), 10000);
        QCOMPARE(backend.mixer()->title(), QString("song"));
        QCOMPARE(backend.error(), QString());

        backend.openPath(dir.filePath("nope.mp3"));
        QVERIFY(backend.error().contains("nope.mp3"));
    }

    void backendSaysWhenDemucsIsMissing()
    {
        QTemporaryDir dir;
        write(dir.filePath("Tune.mp3"), "x");
        FakePicker picker;
        Separator separator(dir.filePath("Stems"));
        separator.setEngine({});
        Backend backend(&picker, &separator);
        QVERIFY(!backend.engineReady());
        backend.openPath(dir.filePath("Tune.mp3"));
        QVERIFY(backend.error().contains("Demucs"));
        QVERIFY(!backend.splitting());
    }

    void pitchShifterMovesPitchNotTempo()
    {
        // Two seconds of 440 Hz, shifted up a half step, should come out
        // two seconds long at about 466 Hz.
        const qint64 frames = 2 * Mixer::SampleRate;
        std::vector<qint16> tone(size_t(frames * 2));
        for (qint64 i = 0; i < frames; ++i)
            tone[size_t(2 * i)] = tone[size_t(2 * i + 1)] = qint16(12000 * std::sin(2 * M_PI * 440 * i / Mixer::SampleRate));
        qint64 cursor = 0;
        auto source = [&](qint16 *into, qint64 wanted) {
            const qint64 count = qBound<qint64>(0, frames - cursor, wanted);
            std::copy_n(tone.begin() + cursor * 2, count * 2, into);
            cursor += count;
            return count;
        };
        PitchShifter shifter(Mixer::SampleRate, 2);
        shifter.restart(100);
        std::vector<qint16> out(size_t(frames * 2));
        QElapsedTimer timer;
        timer.start();
        qint64 made = 0;
        while (made < frames) {
            const qint64 n = shifter.render(out.data() + made * 2, qMin<qint64>(4096, frames - made), source);
            if (n == 0)
                break;
            made += n;
        }
        // Real-time playback needs this far faster than real time.
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QString("2 s took %1 ms").arg(timer.elapsed())));
        QCOMPARE(made, frames);
        QCOMPARE(frequency(out, Mixer::SampleRate / 2, Mixer::SampleRate), 466.16);
    }

    void mixerShiftsPitchWithUndo()
    {
        QTemporaryDir dir;
        Mixer mixer;
        QVERIFY(loadTones(dir, mixer));
        mixer.nudgeCents(10);
        mixer.nudgeCents(10);
        QCOMPARE(mixer.cents(), 20);
        mixer.setCents(500);
        QCOMPARE(mixer.cents(), 100);
        mixer.setCents(-500);
        QCOMPARE(mixer.cents(), -100);
        mixer.undo();
        QCOMPARE(mixer.cents(), 100);
        mixer.resetMix();
        QCOMPARE(mixer.cents(), 0);
    }

    void mixerExportsShiftedPitch()
    {
        QTemporaryDir dir;
        Mixer mixer;
        QVERIFY(loadTones(dir, mixer));
        mixer.toggleSolo(0);  // the 440 Hz tone
        mixer.setCents(-100);
        QCOMPARE(mixer.exportMix(dir.filePath("down.wav")), QString());
        QFile file(dir.filePath("down.wav"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray wav = file.readAll();
        QCOMPARE(wav.size(), 44 + Mixer::SampleRate * 4);
        std::vector<qint16> samples(size_t((wav.size() - 44) / 2));
        memcpy(samples.data(), wav.constData() + 44, samples.size() * 2);
        QCOMPARE(frequency(samples, Mixer::SampleRate / 4, Mixer::SampleRate / 2), 415.30);
    }

    void mixerSavesAndRestoresEachSong()
    {
        QTemporaryDir dir;
        {
            Mixer mixer;
            mixer.setAutosave(true);
            QVERIFY(loadTones(dir, mixer));
            QVERIFY(!mixer.restoreSettings());
            mixer.toggleMute(0);
            mixer.toggleSolo(2);
            mixer.setVolume(1, 0.4);
            mixer.setCents(-30);
            QTRY_VERIFY(QFileInfo::exists(dir.filePath("song/mix.json")));
            mixer.setCents(-35);
            // Leaving the song writes what's pending straight away.
        }
        Mixer mixer;
        QVERIFY(loadTones(dir, mixer, true));
        QCOMPARE(mixer.cents(), 0);
        QVERIFY(mixer.restoreSettings());
        QCOMPARE(mixer.cents(), -35);
        QCOMPARE(mixer.data(mixer.index(0), Mixer::MutedRole).toBool(), true);
        QCOMPARE(mixer.data(mixer.index(2), Mixer::SoloedRole).toBool(), true);
        QCOMPARE(mixer.data(mixer.index(1), Mixer::VolumeRole).toDouble(), 0.4);
        // Restoring is where the song starts, not a step to undo.
        QVERIFY(!mixer.canUndo());

        // Without autosave (the CLI), nothing is written.
        mixer.setCents(50);
        QTest::qWait(600);
        QFile saved(dir.filePath("song/mix.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.readAll().contains("-35"));
    }

    void backendRestoresTheMixOnOpen()
    {
        QTemporaryDir dir;
        QVERIFY(makeTones(dir));
        write(dir.filePath("song/mix.json"), R"({"version":1,"cents":12,"stems":{"drums":{"muted":true}}})");
        FakePicker picker;
        Separator separator(dir.filePath("Stems"), "/nonexistent-demucs");
        Backend backend(&picker, &separator);
        backend.openPath(dir.filePath("song"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.mixer()->loaded(), 10000);
        QCOMPARE(backend.mixer()->cents(), 12);
        QCOMPARE(backend.mixer()->targetGains(), std::vector<float>({1, 0, 1}));
        QVERIFY(!backend.notice().isEmpty());
    }

    void downloaderRecognizesLinks()
    {
        QVERIFY(Downloader::isLink("https://www.youtube.com/watch?v=abc123"));
        QVERIFY(Downloader::isLink("  https://youtu.be/abc123\n"));
        QVERIFY(Downloader::isLink("http://example.com/song"));
        QVERIFY(!Downloader::isLink("youtube.com/watch?v=abc123"));
        QVERIFY(!Downloader::isLink("/home/me/song.mp3"));
        QVERIFY(!Downloader::isLink("file:///home/me/song.mp3"));
        QVERIFY(!Downloader::isLink("https://youtu.be/a b"));
        QVERIFY(!Downloader::isLink("-o evil"));
        QVERIFY(!Downloader::isLink(""));
        // The link always comes after --, so it can never be read as an option.
        const QStringList arguments = Downloader::arguments("/tmp/x", "https://youtu.be/abc");
        QCOMPARE(arguments.mid(arguments.size() - 2), QStringList({"--", "https://youtu.be/abc"}));
        QVERIFY(arguments.contains("--no-playlist"));
    }

    void downloaderParsesProgress()
    {
        QCOMPARE(Downloader::parseProgress("[download]  42.5% of    3.21MiB at  1.00MiB/s ETA 00:02"), 0.425);
        QCOMPARE(Downloader::parseProgress("[download] 100% of    3.21MiB in 00:00:01"), 1.0);
        QCOMPARE(Downloader::parseProgress("[youtube] abc: Downloading webpage"), -1.0);
    }

    void downloaderSavesTheAudio()
    {
        QTemporaryDir dir;
        Downloader downloader(dir.filePath("Downloads"), fakeDownloader(dir));
        QSignalSpy finished(&downloader, &Downloader::finished);
        QSignalSpy titled(&downloader, &Downloader::titled);
        QSignalSpy progress(&downloader, &Downloader::progress);
        downloader.start("https://www.youtube.com/watch?v=abc123");
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toString(), dir.filePath("Downloads/Fake Song.opus"));
        QCOMPARE(titled.first().first().toString(), QString("Fake Song"));
        bool halfway = false;
        for (const auto &signal : progress)
            halfway = halfway || signal.first().toDouble() == 0.5;
        QVERIFY(halfway);
    }

    void downloaderReportsErrors()
    {
        QTemporaryDir dir;
        const QString tool = script(dir, "yt-dlp",
                                    "echo 'ERROR: [youtube] abc: Video unavailable. This video is private'; exit 1\n");
        Downloader downloader(dir.filePath("Downloads"), tool);
        QSignalSpy failed(&downloader, &Downloader::failed);
        downloader.start("https://www.youtube.com/watch?v=abc");
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().contains("This video is private"));

        // Not a link, and no yt-dlp, are both said plainly.
        downloader.start("song.mp3");
        QVERIFY(failed.wait(1000));
        QVERIFY(failed.last().first().toString().contains("song.mp3"));
        Downloader missing(dir.filePath("Downloads"), "");
        if (!Downloader::findTool().isEmpty())
            return;
        QSignalSpy missingFailed(&missing, &Downloader::failed);
        missing.start("https://youtu.be/abc");
        QVERIFY(missingFailed.wait(1000));
        QVERIFY(missingFailed.first().first().toString().contains("yt-dlp"));
    }

    void downloaderCancels()
    {
        QTemporaryDir dir;
        Downloader downloader(dir.filePath("Downloads"), script(dir, "yt-dlp", "sleep 30\n"));
        QSignalSpy failed(&downloader, &Downloader::failed);
        downloader.start("https://youtu.be/abc");
        QVERIFY(downloader.running());
        downloader.cancel();
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().isEmpty());
    }

    void backendDownloadsThenSplitsALink()
    {
        QTemporaryDir dir;
        FakePicker picker;
        const QString demucs = script(dir, "demucs", R"(
out=""; model=""
while [ $# -gt 1 ]; do case "$1" in -o) out="$2"; shift;; -n) model="$2"; shift;; esac; shift; done
echo "Separating track $1"
mkdir -p "$out/$model" && for s in vocals drums bass other; do echo $s > "$out/$model/$s.wav"; done
)");
        Separator separator(dir.filePath("Stems"), demucs);
        Downloader downloader(dir.filePath("Stems/Downloads"), fakeDownloader(dir));
        Backend backend(&picker, &separator, &downloader);

        // The header names the video once yt-dlp knows its title.
        QStringList songs;
        connect(&backend, &Backend::busyChanged, this, [&] { songs << backend.song(); });
        backend.openLink("https://www.youtube.com/watch?v=abc123");
        QVERIFY(backend.downloading());
        QVERIFY(backend.busy());
        QCOMPARE(backend.song(), QString("www.youtube.com"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy() && Separator::isStemsFolder(dir.filePath("Stems/Fake Song")), 5000);
        QVERIFY(songs.contains("Fake Song"));
        QCOMPARE(Separator::stemFiles(dir.filePath("Stems/Fake Song")).size(), 4);
        QVERIFY(QFileInfo::exists(dir.filePath("Stems/Downloads/Fake Song.opus")));

        // Another job can't start while one runs, and a non-link is refused.
        backend.openLink("not a link");
        QVERIFY(backend.error().contains("not a link"));
    }

    void singleInstanceHandsSongsToTheRunningOne()
    {
        QTemporaryDir dir;
        const QString socket = dir.filePath("stems.socket");
        SingleInstance running(socket), later(socket);
        QVERIFY(!later.forward("/music/One.mp3"));

        // A socket left behind by a crash doesn't stop the next Stems.
        write(socket, "stale");
        QVERIFY(running.listen());
        QSignalSpy received(&running, &SingleInstance::received);
        QVERIFY(later.forward("/music/Two Words.mp3"));
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(received.first().first().toString(), QString("/music/Two Words.mp3"));

        // No song just brings the window forward.
        QVERIFY(later.forward(""));
        QTRY_COMPARE(received.size(), 2);
        QCOMPARE(received.last().first().toString(), QString());
    }

    void backendAsksBeforeReplacingASplit()
    {
        QTemporaryDir dir;
        write(dir.filePath("One.mp3"), "x");
        write(dir.filePath("Two.mp3"), "x");
        FakePicker picker;
        // Splitting One never ends on its own; Two splits at once.
        const QString demucs = script(dir, "demucs", R"(
for last; do :; done
case "$last" in *One*) exec sleep 30;; esac
out=""; model=""
while [ $# -gt 1 ]; do case "$1" in -o) out="$2"; shift;; -n) model="$2"; shift;; esac; shift; done
mkdir -p "$out/$model" && for s in vocals drums bass other; do echo $s > "$out/$model/$s.wav"; done
)");
        Separator separator(dir.filePath("Stems"), demucs);
        Backend backend(&picker, &separator);

        backend.openOrAsk(dir.filePath("One.mp3"));
        QVERIFY(backend.splitting());
        QCOMPARE(backend.pendingSong(), QString());

        // Keeping the split forgets the request.
        backend.openOrAsk(dir.filePath("Two.mp3"));
        QCOMPARE(backend.pendingSong(), QString("Two"));
        QCOMPARE(backend.song(), QString("One"));
        backend.keepCurrent();
        QCOMPARE(backend.pendingSong(), QString());
        QVERIFY(backend.splitting());

        // Replacing stops One and splits Two.
        backend.openOrAsk(dir.filePath("Two.mp3"));
        backend.replaceWithPending();
        QTRY_VERIFY_WITH_TIMEOUT(Separator::isStemsFolder(dir.filePath("Stems/Two")), 5000);
        QCOMPARE(backend.pendingSong(), QString());
        QVERIFY(!Separator::isStemsFolder(dir.filePath("Stems/One")));
        QCOMPARE(backend.error(), QString());
    }

private:
    // A stand-in for yt-dlp that prints what the real one does and saves a
    // file where its -o template says.
    static QString fakeDownloader(QTemporaryDir &dir)
    {
        return script(dir, "yt-dlp", R"(
template=""
while [ $# -gt 0 ]; do case "$1" in -o) template="$2"; shift;; esac; shift; done
folder=$(dirname "$template")
mkdir -p "$folder"
echo "stems-title:Fake Song"
echo "[download]  50.0% of 1.00MiB at 1.00MiB/s ETA 00:01"
echo "[download] 100% of 1.00MiB in 00:00:01"
echo "audio" > "$folder/Fake Song.opus"
echo "stems-file:$folder/Fake Song.opus"
)");
    }

    class FakePicker : public FilePicker {
    public:
        void openAudio(const QString &) override {}
        void saveMix(const QUrl &) override {}
    };

    // The tone's frequency from its zero crossings over [from, to) frames of
    // the left channel, rounded to hundredths of a hertz within 0.5 %.
    static double frequency(const std::vector<qint16> &stereo, qint64 from, qint64 to)
    {
        double first = -1, last = -1;
        int crossings = 0;
        for (qint64 i = from + 1; i < to; ++i) {
            const qint16 a = stereo[size_t(2 * (i - 1))], b = stereo[size_t(2 * i)];
            if (a < 0 && b >= 0) {
                const double at = i - 1 + double(-a) / (b - a);
                if (first < 0) first = at; else ++crossings;
                last = at;
            }
        }
        const double hz = crossings * double(Mixer::SampleRate) / (last - first);
        // Snap to the expected values so QCOMPARE can name them exactly.
        for (const double expected : {466.16, 415.30, 440.0})
            if (std::abs(hz - expected) / expected < 0.005)
                return expected;
        return hz;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    static QString script(QTemporaryDir &dir, const QString &name, const QByteArray &body)
    {
        const QString path = dir.filePath(name);
        write(path, "#!/bin/sh\n" + body);
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    // Three one-second tones in dir/song, made with ffmpeg.
    static bool makeTones(QTemporaryDir &dir)
    {
        QDir().mkpath(dir.filePath("song"));
        const QList<QPair<QString, int>> tones{{"vocals", 440}, {"drums", 110}, {"bass", 55}};
        for (const auto &[name, hz] : tones) {
            const QString path = dir.filePath("song/" + name + ".wav");
            if (QFileInfo::exists(path))
                continue;
            if (QProcess::execute("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
                                             QString("sine=f=%1:d=1:sample_rate=44100").arg(hz), "-ac", "2", path}) != 0)
                return false;
        }
        return true;
    }

    static bool loadTones(QTemporaryDir &dir, Mixer &mixer, bool existing = false)
    {
        if (!existing && !makeTones(dir))
            return false;
        QSignalSpy loaded(&mixer, &Mixer::loadedChanged);
        mixer.load(Separator::stemFiles(dir.filePath("song")), dir.filePath("song"), "song");
        for (int i = 0; i < 100 && !mixer.loaded(); ++i)
            loaded.wait(100);
        return mixer.loaded();
    }
};

QTEST_MAIN(StemsTests)
#include "stems_tests.moc"
