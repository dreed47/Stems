#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "backend.h"
#include "mixer.h"
#include "pitchshifter.h"
#include "separator.h"
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

private:
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
