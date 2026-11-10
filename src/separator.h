#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <memory>

class QTemporaryDir;

// Splits one song into stems by running Demucs, and knows where stems live:
// one folder per song under ~/Music/Stems, holding vocals.wav, drums.wav and
// the rest. The original file is only ever read.
class Separator : public QObject {
    Q_OBJECT

public:
    explicit Separator(const QString &stemsRoot = {}, const QString &demucs = {}, QObject *parent = nullptr);
    ~Separator() override;

    QString stemsRoot() const { return m_root; }
    QString engine() const { return m_engine; }
    void setEngine(const QString &path) { m_engine = path; }
    bool running() const { return m_process.state() != QProcess::NotRunning; }

    // The stems folder a song splits into; the 6-stem model gets its own.
    QString folderFor(const QString &input, bool sixStems) const;

    // Starts a split. A song that was split before finishes at once.
    void start(const QString &input, bool sixStems);
    void cancel();

    static QString findEngine();
    static QString defaultRoot();
    static QString model(bool sixStems) { return sixStems ? "htdemucs_6s" : "htdemucs"; }
    static const QStringList &knownStems();
    // The stem WAVs in a folder, in the order the mixer shows them.
    static QStringList stemFiles(const QString &folder);
    static bool isStemsFolder(const QString &folder) { return stemFiles(folder).size() >= 2; }
    // Demucs reports each model's progress as its own tqdm bar; this turns
    // one chunk of its stderr into an overall fraction (or -1 for none).
    static double parseProgress(const QString &chunk, int *bars, int *barIndex, double *lastPercent);

signals:
    void progress(double fraction, const QString &status);
    void finished(const QString &folder);
    void failed(const QString &message);

private:
    void handleOutput();
    void handleFinished(int code, QProcess::ExitStatus status);

    QString m_root;
    QString m_engine;
    QString m_target;
    QString m_model;
    QProcess m_process;
    std::unique_ptr<QTemporaryDir> m_work;
    QString m_log;
    bool m_cancelled = false;
    bool m_separating = false;
    int m_bars = 1;
    int m_barIndex = 0;
    double m_lastPercent = 0;
};
