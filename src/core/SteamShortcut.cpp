#include "core/SteamShortcut.h"

#include "core/HostRun.h"
#include "core/Location.h"

#include <QFileInfo>
#include <QUrl>

namespace SteamShortcut {

bool canAdd(const QString &path)
{
    if (!Location::isLocal(path))
        return false;
    const QFileInfo fi(path);
    if (!fi.isFile())
        return false;
    static const QStringList exts = {QStringLiteral("exe"), QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("msi"),
                                     QStringLiteral("lnk"), QStringLiteral("appimage"), QStringLiteral("sh"), QStringLiteral("desktop"),
                                     QStringLiteral("x86_64"), QStringLiteral("run")};
    return exts.contains(fi.suffix().toLower()) || fi.isExecutable();
}

QString addUrl(const QString &path)
{
    return QStringLiteral("steam://addnonsteamgame/") + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

QString hostScript()
{
    // SteamOS's own tool when it is there (what Dolphin's menu runs);
    // otherwise Steam's link, which it only takes with this file present.
    return QStringLiteral("if command -v steamos-add-to-steam >/dev/null 2>&1; then exec steamos-add-to-steam \"$1\"; fi; "
                          "touch /tmp/addnonsteamgamefile; exec xdg-open \"$2\"");
}

bool add(const QString &path, QString *error)
{
    if (!canAdd(path)) {
        if (error)
            *error = QStringLiteral("%1 is not a program Steam can start.").arg(Location::fileName(path));
        return false;
    }
    return HostRun::start(QStringLiteral("sh"), {QStringLiteral("-c"), hostScript(), QStringLiteral("sh"), path, addUrl(path)},
                          Location::parent(path), error);
}

} // namespace SteamShortcut
