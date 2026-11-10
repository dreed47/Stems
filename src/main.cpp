// stems — split a song into its instruments and play them back, soloing and
// muting each one.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "separator.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    // Commands run without a display, so they work over ssh and for agents.
    const bool command = Cli::isCommand(arguments);
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("stems");
    app.setOrganizationName("stems");
    app.setApplicationVersion("0.2.0");

    // Matches the window to stems.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("stems");
    app.setWindowIcon(QIcon::fromTheme("stems"));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    if (command)
        return Cli::run(arguments);

    QQuickStyle::setStyle("Material");

    Theme theme;
    PortalFilePicker picker;
    Separator separator;
    Backend backend(&picker, &separator);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("mixer", backend.mixer());
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // `stems FILE` and `stems open FILE` both start with that song.
    QStringList rest = arguments.mid(1);
    if (rest.value(0) == "open")
        rest.removeFirst();
    if (!rest.isEmpty())
        backend.openPath(rest.first());

    return app.exec();
}
