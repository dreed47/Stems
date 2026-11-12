#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSettings>
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
#include "tempo.h"
#include "theme.h"
#include "translator.h"

class StemsTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Settings the tests write must never reach the real ~/.config.
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings;
        QVERIFY2(settings.fileName().contains(".qttest"), qPrintable(settings.fileName()));
        settings.clear();
    }

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
        backend.splitAgain();
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

    void tempoFindsTheBeatInDrums()
    {
        for (const double bpm : {90.0, 128.0, 160.0}) {
            std::vector<double> truth;
            const std::vector<qint16> pcm = drumLoop(bpm, 40, 2.0, &truth);
            const Tempo::Grid grid = Tempo::detect({{pcm.data(), qint64(pcm.size() / 2)}}, qint64(pcm.size() / 2), Mixer::SampleRate);
            QVERIFY2(std::abs(grid.bpm - bpm) < 0.2, qPrintable(QString::number(grid.bpm)));
            // Every beat lands within 15 ms of a hit, and none are missed.
            QCOMPARE(grid.beats.size(), truth.size());
            for (size_t i = 0; i < truth.size(); ++i)
                QVERIFY2(std::abs(grid.beats[i] - truth[i]) < 0.015, qPrintable(QString::number(grid.beats[i])));
        }
    }

    void tempoFindsNoBeatInASteadyTone()
    {
        std::vector<qint16> sine(size_t(Mixer::SampleRate) * 20 * 2);
        for (size_t i = 0; i < sine.size() / 2; ++i)
            sine[2 * i] = sine[2 * i + 1] = qint16(8000 * std::sin(2 * M_PI * 110 * double(i) / Mixer::SampleRate));
        QVERIFY(!Tempo::detect({{sine.data(), qint64(sine.size() / 2)}}, qint64(sine.size() / 2), Mixer::SampleRate).valid());
        std::vector<qint16> silence(size_t(Mixer::SampleRate) * 20 * 2, 0);
        QVERIFY(!Tempo::detect({{silence.data(), qint64(silence.size() / 2)}}, qint64(silence.size() / 2), Mixer::SampleRate).valid());
    }

    void tempoTapsAverageOut()
    {
        QCOMPARE(Tempo::tappedBpm({}), 0.0);
        QCOMPARE(Tempo::tappedBpm({1.0}), 0.0);
        QCOMPARE(Tempo::tappedBpm({0, 0.5}), 120.0);
        // One late tap only nudges it.
        QVERIFY(std::abs(Tempo::tappedBpm({0, 0.5, 1.04, 1.5, 2.0}) - 120) < 1.5);
    }

    void tempoPlansACountIn()
    {
        std::vector<double> beats;
        for (double t = 1.0; t < 20; t += 0.5)
            beats.push_back(t);
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };

        // Mid-song, back to the beat at or before the playhead.
        Tempo::CountIn plan = Tempo::plan(beats, 120, 5.3, 4);
        QVERIFY(near(plan.start, 5.0) && near(plan.preroll, 2.0) && near(plan.interval, 0.5));
        // Just short of a beat counts in to that beat.
        plan = Tempo::plan(beats, 120, 4.95, 4);
        QVERIFY(near(plan.start, 5.0) && near(plan.preroll, 2.0));
        // Before the drums come in, the grid carries on backward.
        plan = Tempo::plan(beats, 120, 0, 2);
        QVERIFY(near(plan.start, 0) && near(plan.preroll, 1.0));
        // A beat just before the song starts: the clicks lead into it, and
        // the song comes in a moment after the last click.
        for (double &b : beats)
            b -= 0.3;
        plan = Tempo::plan(beats, 120, 0, 4);
        QVERIFY(near(plan.start, 0) && near(plan.preroll, 2.3));
        // Past the last beat, it carries on forward.
        plan = Tempo::plan(beats, 120, 30.1, 4);
        QVERIFY(near(plan.start, 29.7) && near(plan.preroll, 2.0));
        // A tapped tempo with no beats counts in to the playhead.
        plan = Tempo::plan({}, 100, 12.5, 4);
        QVERIFY(near(plan.start, 12.5) && near(plan.preroll, 2.4) && near(plan.interval, 0.6));
        QVERIFY(!Tempo::plan({}, 0, 12.5, 4).valid());

        // An intro before the drums: the grid carried back from the drums
        // has a beat in the silence before the first note (-0.3 here), but
        // the count leads into the note itself, a moment after the start.
        plan = Tempo::plan(beats, 120, 0, 4, 0.32);
        QVERIFY(near(plan.start, 0.29) && near(plan.preroll, 2.0 - 0.03) && near(plan.interval, 0.5));
        // Anywhere in the silence before it, too; past it, back to the grid.
        plan = Tempo::plan(beats, 120, 0.31, 4, 0.32);
        QVERIFY(near(plan.start, 0.31) && near(plan.preroll, 2.0 - 0.01));
        plan = Tempo::plan(beats, 120, 5.3, 4, 0.32);
        QVERIFY(near(plan.start, 5.2) && near(plan.preroll, 2.0));
    }

    void tempoFindsTheFirstNote()
    {
        std::vector<qint16> quiet(size_t(Mixer::SampleRate) * 2, 0), late(size_t(Mixer::SampleRate) * 2, 0);
        // Demucs bleed stays below the line; a note at 0.5 s doesn't.
        for (size_t i = 0; i < quiet.size(); ++i)
            quiet[i] = qint16(i % 7 * 40 - 120);
        for (size_t f = size_t(Mixer::SampleRate / 2); f < late.size() / 2; ++f)
            late[2 * f] = late[2 * f + 1] = 6000;
        const qint64 frames = Mixer::SampleRate;
        QVERIFY(std::abs(Tempo::firstSound({{quiet.data(), frames}, {late.data(), frames}}, Mixer::SampleRate) - 0.495) < 1e-3);
        QCOMPARE(Tempo::firstSound({{quiet.data(), frames}}, Mixer::SampleRate), -1.0);
    }

    void mixerFindsTempoAndKeepsTaps()
    {
        QTemporaryDir dir;
        QVERIFY(makeTones(dir));
        // Replace the drums tone with a drum loop.
        const std::vector<qint16> loop = drumLoop(120, 12, 0.5);
        writeWav(dir.filePath("song/drums.wav"), loop);
        {
            Mixer mixer;
            mixer.setAutosave(true);
            QVERIFY(loadTones(dir, mixer, true));
            QVERIFY2(std::abs(mixer.bpm() - 120) < 0.2, qPrintable(QString::number(mixer.bpm())));
            QVERIFY(!mixer.tempoTapped());
            QVERIFY(mixer.beats().size() > 15);

            QSignalSpy changed(&mixer, &Mixer::tempoChanged);
            mixer.tap();
            QVERIFY(!mixer.tempoTapped());
            QTest::qWait(400);
            mixer.tap();
            QTest::qWait(400);
            mixer.tap();
            QVERIFY(mixer.tempoTapped());
            QVERIFY2(std::abs(mixer.bpm() - 150) < 15, qPrintable(QString::number(mixer.bpm())));
            QCOMPARE(changed.count(), 2);
            // Tapped while stopped, so there's no beat grid, just a tempo.
            QVERIFY(mixer.beats().empty());
            QTRY_VERIFY(QFileInfo::exists(dir.filePath("song/mix.json")));
        }
        Mixer mixer;
        QVERIFY(loadTones(dir, mixer, true));
        QVERIFY(mixer.restoreSettings());
        QVERIFY(mixer.tempoTapped());
        QVERIFY(std::abs(mixer.bpm() - 150) < 15);
        mixer.clearTap();
        QVERIFY(!mixer.tempoTapped());
        QVERIFY(std::abs(mixer.bpm() - 120) < 0.2);

        // The tones alone have no pulse.
        QTemporaryDir plain;
        Mixer tones;
        QVERIFY(loadTones(plain, tones));
        QCOMPARE(tones.bpm(), 0.0);
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
        backend.splitAgain();
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
        QTRY_COMPARE(backend.askSong(), QString("Two"));
        backend.splitAgain();
        QTRY_VERIFY_WITH_TIMEOUT(Separator::isStemsFolder(dir.filePath("Stems/Two")), 5000);
        QCOMPARE(backend.pendingSong(), QString());
        QVERIFY(!Separator::isStemsFolder(dir.filePath("Stems/One")));
        QCOMPARE(backend.error(), QString());
    }

    void separatorSplitsAgainKeepingOtherFiles()
    {
        QTemporaryDir dir;
        QVERIFY(makeTones(dir));
        write(dir.filePath("Tune.mp3"), "x");
        const QString demucs = toneDemucs(dir);
        Separator separator(dir.filePath("Stems"), demucs);
        QSignalSpy finished(&separator, &Separator::finished);
        separator.start(dir.filePath("Tune.mp3"), false);
        QVERIFY(finished.wait(5000));
        const QString folder = dir.filePath("Stems/Tune");
        QCOMPARE(Separator::stemFiles(folder).size(), 3);
        write(folder + "/mix.json", "{}");

        // Splitting again swaps the stems but keeps the saved mix.
        separator.start(dir.filePath("Tune.mp3"), false, true);
        QVERIFY(separator.running());
        QVERIFY(finished.wait(5000));
        QCOMPARE(Separator::stemFiles(folder).size(), 4);
        QVERIFY(QFileInfo::exists(folder + "/mix.json"));
        QCOMPARE(QDir(dir.filePath("Stems")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
                 QStringList{"Tune"});
    }

    void replaceStemsPutsTheOldOnesBack()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("old/other.wav"));
        QDir().mkpath(dir.filePath("new"));
        write(dir.filePath("old/other.wav/keep"), "x");
        for (const auto *name : {"vocals", "drums", "other"})
            write(dir.filePath(QString("new/%1.wav").arg(name)), "new");
        for (const auto *name : {"vocals", "drums"})
            write(dir.filePath(QString("old/%1.wav").arg(name)), "old");
        // A folder where the last new stem has to go makes the move fail part way.
        QVERIFY(!Separator::replaceStems(dir.filePath("new"), dir.filePath("old"), dir.filePath("backup")));
        QCOMPARE(Separator::stemFiles(dir.filePath("old")).size(), 2);
        for (const auto &file : Separator::stemFiles(dir.filePath("old"))) {
            QFile in(file);
            QVERIFY(in.open(QIODevice::ReadOnly));
            QCOMPARE(in.readAll(), QByteArray("old"));
        }
    }

    void backendAsksBeforeSplittingAgain()
    {
        QTemporaryDir dir;
        QVERIFY(makeTones(dir));
        write(dir.filePath("Tune.mp3"), "x");
        FakePicker picker;
        Separator separator(dir.filePath("Stems"), toneDemucs(dir));
        Backend backend(&picker, &separator);
        backend.setSixStems(false);

        // Every song asks 4 or 6 first; this one has no stems for either.
        backend.openPath(dir.filePath("Tune.mp3"));
        QCOMPARE(backend.askKind(), int(Backend::AskOpen));
        QVERIFY(!backend.askHasStems());
        QVERIFY(!backend.busy());
        backend.splitAgain();
        QTRY_VERIFY_WITH_TIMEOUT(backend.mixer()->loaded(), 10000);
        QCOMPARE(backend.askKind(), int(Backend::AskNone));
        QCOMPARE(backend.source(), dir.filePath("Tune.mp3"));

        // Opening it again: it has 4 stems, not 6. Opening what's there doesn't split.
        backend.openPath(dir.filePath("Tune.mp3"));
        QCOMPARE(backend.askSong(), QString("Tune"));
        QVERIFY(backend.askHasStems());
        backend.setSixStems(true);
        QVERIFY(!backend.askHasStems());
        backend.setSixStems(false);
        QVERIFY(!backend.busy());
        backend.openExisting();
        QCOMPARE(backend.askKind(), int(Backend::AskNone));
        QVERIFY(!backend.busy());
        QTRY_COMPARE_WITH_TIMEOUT(backend.mixer()->rowCount(), 3, 10000);

        // Split again, from inside the app, replaces the stems.
        backend.resplit();
        QCOMPARE(backend.askKind(), int(Backend::AskOpen));
        backend.splitAgain();
        QVERIFY(backend.splitting());
        QTRY_COMPARE_WITH_TIMEOUT(backend.mixer()->rowCount(), 4, 10000);

        // Cancel does nothing.
        backend.openPath(dir.filePath("Tune.mp3"));
        backend.dismissAsk();
        QCOMPARE(backend.askKind(), int(Backend::AskNone));
        QVERIFY(!backend.busy());
    }

    void backendSwitchesStemCount()
    {
        QTemporaryDir dir;
        QVERIFY(makeTones(dir));
        write(dir.filePath("Tune.mp3"), "x");
        FakePicker picker;
        Separator separator(dir.filePath("Stems"), toneDemucs(dir));
        Backend backend(&picker, &separator);
        backend.setSixStems(false);
        backend.openPath(dir.filePath("Tune.mp3"));
        backend.splitAgain();
        QTRY_VERIFY_WITH_TIMEOUT(backend.mixer()->loaded(), 10000);

        // No 6-stem split yet: it asks, and cancel puts the count back.
        backend.switchStems(true);
        QCOMPARE(backend.askKind(), int(Backend::AskOtherCount));
        backend.dismissAsk();
        QVERIFY(!backend.sixStems());

        backend.switchStems(true);
        backend.splitAgain();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy() && backend.mixer()->loaded(), 10000);
        QCOMPARE(backend.mixer()->folder(), dir.filePath("Stems/Tune (6 stems)"));
        QVERIFY(backend.sixStems());

        // Both splits exist now, so switching just loads the other one.
        backend.switchStems(false);
        QCOMPARE(backend.askKind(), int(Backend::AskNone));
        QTRY_COMPARE_WITH_TIMEOUT(backend.mixer()->folder(), dir.filePath("Stems/Tune"), 10000);

        // The same works for stems opened as a folder.
        backend.openPath(dir.filePath("Stems/Tune"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.mixer()->loaded(), 10000);
        backend.switchStems(true);
        QTRY_COMPARE_WITH_TIMEOUT(backend.mixer()->folder(), dir.filePath("Stems/Tune (6 stems)"), 10000);
    }

    void backendSetsTheStemsFolder()
    {
        QTemporaryDir dir;
        FakePicker picker;
        Separator separator(dir.filePath("Stems"), "/nonexistent-demucs");
        Downloader downloader(dir.filePath("Stems/Downloads"), "/bin/false");
        Backend backend(&picker, &separator, &downloader);
        QSignalSpy changed(&backend, &Backend::stemsRootChanged);

        backend.setStemsRoot(dir.filePath("Elsewhere/"));
        QCOMPARE(backend.stemsRoot(), dir.filePath("Elsewhere"));
        QCOMPARE(separator.folderFor("/music/A.mp3", false), dir.filePath("Elsewhere/A"));
        QCOMPARE(downloader.folder(), dir.filePath("Elsewhere/Downloads"));
        QCOMPARE(Separator::defaultRoot(), dir.filePath("Elsewhere"));
        QVERIFY(backend.customStemsRoot());
        QCOMPARE(changed.size(), 1);

        backend.resetStemsRoot();
        QCOMPARE(backend.stemsRoot(), Separator::builtinRoot());
        QVERIFY(!QSettings().contains("stemsRoot"));
        QVERIFY(!backend.customStemsRoot());
    }

    void backendListsDependencies()
    {
        QTemporaryDir dir;
        FakePicker picker;
        Separator separator(dir.filePath("Stems"), "/nonexistent-demucs");
        Backend backend(&picker, &separator);
        backend.refreshDependencies();
        QStringList names;
        for (const auto &entry : backend.dependencies())
            names << entry.toMap().value("name").toString();
        QCOMPARE(names, QStringList({"Demucs", "ffmpeg", "uv", "yt-dlp"}));
        // ffmpeg is on every machine that runs these tests (makeTones needs it).
        QTRY_VERIFY_WITH_TIMEOUT(!backend.dependencies()[1].toMap().value("version").toString().isEmpty(), 5000);
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
        void chooseFolder(const QString &) override {}
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

    // A drum loop: kick and snare on alternate beats, hats on the eighths,
    // starting after `intro` seconds of silence; `truth` gets the beats.
    static std::vector<qint16> drumLoop(double bpm, double seconds, double intro, std::vector<double> *truth = nullptr)
    {
        const int rate = Mixer::SampleRate;
        std::vector<float> mix(size_t(seconds * rate), 0.f);
        quint32 noise = 1;
        auto random = [&] { noise = noise * 1664525u + 1013904223u; return double(noise) / 2147483648.0 - 1; };
        auto hit = [&](double at, int kind, double level) {
            const size_t from = size_t(at * rate);
            for (size_t i = 0; i < size_t(rate / 4) && from + i < mix.size(); ++i) {
                const double t = double(i) / rate;
                const double v = kind == 0 ? std::sin(2 * M_PI * (50 + 80 * std::exp(-t * 30)) * t) * std::exp(-t * 8)
                               : kind == 1 ? (0.6 * random() + 0.4 * std::sin(2 * M_PI * 190 * t)) * std::exp(-t * 18)
                                           : 0.4 * random() * std::exp(-t * 60);
                mix[from + i] += float(v * level);
            }
        };
        const double interval = 60 / bpm;
        for (int beat = 0; intro + beat * interval < seconds - 0.3; ++beat) {
            const double at = intro + beat * interval;
            if (truth)
                truth->push_back(at);
            hit(at, beat % 2, 0.8);
            hit(at, 2, 0.5);
            hit(at + interval / 2, 2, 0.35);
        }
        std::vector<qint16> stereo(mix.size() * 2);
        for (size_t i = 0; i < mix.size(); ++i)
            stereo[2 * i] = stereo[2 * i + 1] = qint16(std::clamp(mix[i] * 16000.f, -32768.f, 32767.f));
        return stereo;
    }

    static void writeWav(const QString &path, const std::vector<qint16> &stereo)
    {
        const quint32 bytes = quint32(stereo.size() * 2);
        QByteArray wav(44, 0);
        memcpy(wav.data(), "RIFF", 4);
        qToLittleEndian<quint32>(36 + bytes, wav.data() + 4);
        memcpy(wav.data() + 8, "WAVEfmt ", 8);
        qToLittleEndian<quint32>(16, wav.data() + 16);
        qToLittleEndian<quint16>(1, wav.data() + 20);
        qToLittleEndian<quint16>(2, wav.data() + 22);
        qToLittleEndian<quint32>(Mixer::SampleRate, wav.data() + 24);
        qToLittleEndian<quint32>(Mixer::SampleRate * 4, wav.data() + 28);
        qToLittleEndian<quint16>(4, wav.data() + 32);
        qToLittleEndian<quint16>(16, wav.data() + 34);
        memcpy(wav.data() + 36, "data", 4);
        qToLittleEndian<quint32>(bytes, wav.data() + 40);
        wav.append(reinterpret_cast<const char *>(stereo.data()), qsizetype(bytes));
        write(path, wav);
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
    // A stand-in for Demucs that copies the test tones in as stems: three
    // the first time, four (with "other") every time after.
    static QString toneDemucs(QTemporaryDir &dir)
    {
        return script(dir, "demucs", QString(R"(
out=""; model=""
while [ $# -gt 1 ]; do case "$1" in -o) out="$2"; shift;; -n) model="$2"; shift;; esac; shift; done
echo "Separating track $1"
mkdir -p "$out/$model" && cp "%1"/*.wav "$out/$model/"
[ -e "%1.ran" ] && cp "%1/vocals.wav" "$out/$model/other.wav"
touch "%1.ran"
)").arg(dir.filePath("song")).toUtf8());
    }

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
