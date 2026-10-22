#include "core/AliasManager.h"
#ifdef LINOPUS_HAVE_KIO
#include "kio/KioBackend.h"
#endif
#include "ui/AppContext.h"
#include "ui/ListerWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Linopus"));
    QApplication::setApplicationVersion(QStringLiteral(LINOPUS_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Linopus"));
    QApplication::setDesktopFileName(QStringLiteral(LINOPUS_APP_ID));

    AppContext::setupIconTheme();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Source and destination file manager"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Open a new lister at this folder."));
    parser.process(app);

#ifdef LINOPUS_HAVE_KIO
    KioBackend kio; // servers and archives
#endif
    AppContext context;
    const QStringList folders = parser.positionalArguments();
    if (!folders.isEmpty()) {
        for (const QString &f : folders) {
            QString path = context.aliases().resolve(f);
            if (!QDir::isAbsolutePath(path))
                path = QDir::current().absoluteFilePath(path);
            if (!QFileInfo(path).isDir())
                path = QFileInfo(path).absolutePath();
            context.newLister(QDir::cleanPath(path));
        }
    } else {
        context.restoreSession();
    }
    return app.exec();
}
