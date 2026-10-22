#include "core/AliasManager.h"
#ifdef LINOPUS_HAVE_KIO
#include "kio/KioBackend.h"
#endif
#include "ui/AppContext.h"
#include "ui/Launch.h"
#include "ui/ListerWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Linopus"));
    QApplication::setApplicationVersion(QStringLiteral(LINOPUS_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Linopus"));
    QApplication::setDesktopFileName(QStringLiteral(LINOPUS_APP_ID));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Source and destination file manager.\n"
                                                    "When Linopus is already running, it gets the folders and commands."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("locations"),
                                 QStringLiteral("Folders (or files, to show them in their folder), aliases like /sdcard, "
                                                "file:// links or server addresses to open."),
                                 QStringLiteral("[locations...]"));
    const QCommandLineOption tab(QStringLiteral("tab"), QStringLiteral("Open them in new tabs of the active lister."));
    const QCommandLineOption cmd(QStringList{QStringLiteral("c"), QStringLiteral("cmd")},
                                 QStringLiteral("Run a Linopus command in the active lister (more than once is fine)."),
                                 QStringLiteral("command"));
    const QCommandLineOption separate(QStringLiteral("new-instance"),
                                      QStringLiteral("Start a separate copy even if Linopus is running."));
    parser.addOptions({tab, cmd, separate});
    parser.process(app);

    Launch::Request request;
    request.args = parser.positionalArguments();
    request.cwd = QDir::currentPath();
    request.commands = parser.values(cmd);
    request.tab = parser.isSet(tab);
    request.activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");

    // Already running: hand it over and stop here.
    if (!parser.isSet(separate) && Launch::sendToRunning(request))
        return 0;

    AppContext::setupIconTheme();
#ifdef LINOPUS_HAVE_KIO
    KioBackend kio; // servers and archives
#endif
    AppContext context;
    InstanceServer server;
    if (!parser.isSet(separate)) {
        server.listen();
        QObject::connect(&server, &InstanceServer::requested, &context, &AppContext::handleLaunch);
    }
    if (request.args.isEmpty() && request.commands.isEmpty())
        context.restoreSession();
    else
        context.handleLaunch(request);
    return app.exec();
}
