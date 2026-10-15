#include "tools/AppState.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("pane"));
    QApplication::setApplicationName(QStringLiteral("pane"));
    QApplication::setApplicationDisplayName(QStringLiteral("Pane"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setDesktopFileName(QStringLiteral("pane"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("An image editor in the style of paint.net"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Images to open."), QStringLiteral("[files...]"));
    parser.process(app);

    pane::AppState::instance().load();
    pane::ThemeEngine &themes = pane::ThemeEngine::instance();
    themes.loadCustomThemes();
    QSettings s;
    QString themeId = s.value(QStringLiteral("Theme/id"), themes.systemDefaultThemeId()).toString();
    themes.apply(themeId, s.value(QStringLiteral("Theme/accent")).toString());

    pane::MainWindow w;
    w.show();
    w.checkRecovery();
    if (!parser.positionalArguments().isEmpty()) w.openFiles(parser.positionalArguments());
    return app.exec();
}
