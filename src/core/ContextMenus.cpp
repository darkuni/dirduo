#include "core/ContextMenus.h"

#include "core/FileKind.h"
#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

ContextMenus *ContextMenus::instance()
{
    static ContextMenus *c = new ContextMenus;
    return c;
}

ContextMenus::ContextMenus()
{
    reload();
}

QString ContextMenus::file() const
{
    return Paths::configFile(QStringLiteral("contextmenu.json"));
}

void ContextMenus::reload()
{
    m_items.clear();
    m_hidden.clear();
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        Item i;
        i.label = o.value(QStringLiteral("label")).toString();
        i.icon = o.value(QStringLiteral("icon")).toString();
        i.command = o.value(QStringLiteral("command")).toString();
        i.applies = o.value(QStringLiteral("applies")).toString(QStringLiteral("files"));
        i.submenu = o.value(QStringLiteral("submenu")).toString();
        i.several = o.value(QStringLiteral("several")).toBool(true);
        i.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (!i.label.trimmed().isEmpty() && !i.command.trimmed().isEmpty())
            m_items << i;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("hidden")).toArray())
        m_hidden << canonical(v.toString());
}

void ContextMenus::write()
{
    QJsonArray items;
    for (const Item &i : std::as_const(m_items)) {
        QJsonObject o;
        o.insert(QStringLiteral("label"), i.label);
        if (!i.icon.isEmpty())
            o.insert(QStringLiteral("icon"), i.icon);
        o.insert(QStringLiteral("command"), i.command);
        o.insert(QStringLiteral("applies"), i.applies);
        if (!i.submenu.isEmpty())
            o.insert(QStringLiteral("submenu"), i.submenu);
        if (!i.several)
            o.insert(QStringLiteral("several"), false);
        if (!i.enabled)
            o.insert(QStringLiteral("enabled"), false);
        items.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("items"), items);
    root.insert(QStringLiteral("hidden"), QJsonArray::fromStringList(m_hidden));
    QDir().mkpath(QFileInfo(file()).absolutePath());
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
}

void ContextMenus::set(const QList<Item> &items, const QStringList &hidden)
{
    m_items = items;
    m_hidden.clear();
    for (const QString &h : hidden)
        if (!m_hidden.contains(canonical(h)))
            m_hidden << canonical(h);
    write();
    Q_EMIT changed();
}

QStringList ContextMenus::builtInLabels()
{
    return {QStringLiteral("Open"),
            QStringLiteral("Open in New Tab"),
            QStringLiteral("Open in Destination"),
            QStringLiteral("Open as Folder"),
            QStringLiteral("Open as Folder in New Tab"),
            QStringLiteral("Open in Application"),
            QStringLiteral("Open With"),
            QStringLiteral("Add to Steam"),
            QStringLiteral("Cut"),
            QStringLiteral("Copy"),
            QStringLiteral("Paste"),
            QStringLiteral("Copy to Destination"),
            QStringLiteral("Move to Destination"),
            QStringLiteral("Rename"),
            QStringLiteral("Move to Trash"),
            QStringLiteral("Delete Permanently"),
            QStringLiteral("Extract Here"),
            QStringLiteral("Extract Here and Trash Archive"),
            QStringLiteral("Extract to Destination"),
            QStringLiteral("Extract to \"folder\""),
            QStringLiteral("Add to Archive"),
            QStringLiteral("Add to \"name.zip\""),
            QStringLiteral("View"),
            QStringLiteral("Play"),
            QStringLiteral("Convert Image"),
            QStringLiteral("New Folder"),
            QStringLiteral("Show Hidden Files"),
            QStringLiteral("Open Konsole in This Folder"),
            QStringLiteral("Make Link in Destination"),
            QStringLiteral("Make Link Here"),
            QStringLiteral("Label"),
            QStringLiteral("Rating"),
            QStringLiteral("Tags and Description"),
            QStringLiteral("Verify Checksums"),
            QStringLiteral("Join Pieces"),
            QStringLiteral("Checksums"),
            QStringLiteral("Properties"),
            QStringLiteral("Folder Properties")};
}

QString ContextMenus::canonical(const QString &text)
{
    QString t = text;
    t.remove(QLatin1Char('&'));
    t = t.trimmed();
    if (t.endsWith(QLatin1String("...")))
        t.chop(3);
    t = t.trimmed();
    if (t.startsWith(QLatin1String("Extract to \"")) || t == QLatin1String("Extract Each to Its Own Folder"))
        return QStringLiteral("Extract to \"folder\"");
    if (t.startsWith(QLatin1String("Add to \"")))
        return QStringLiteral("Add to \"name.zip\"");
    if (t == QLatin1String("Convert Images"))
        return QStringLiteral("Convert Image");
    return t;
}

QList<QPair<QString, QString>> ContextMenus::appliesChoices()
{
    QList<QPair<QString, QString>> out{{QStringLiteral("all"), QStringLiteral("Anything (files and folders)")},
                                       {QStringLiteral("files"), QStringLiteral("Files")},
                                       {QStringLiteral("folders"), QStringLiteral("Folders")},
                                       {QStringLiteral("background"), QStringLiteral("Empty space in a list")}};
    for (FileKind::Category c : {FileKind::Category::Image, FileKind::Category::Audio, FileKind::Category::Video, FileKind::Category::Archive,
                                 FileKind::Category::DiskImage, FileKind::Category::Document, FileKind::Category::Code,
                                 FileKind::Category::Executable})
        out << qMakePair(FileKind::categoryKey(c), FileKind::categoryName(c));
    return out;
}

QString ContextMenus::appliesText(const QString &applies)
{
    for (const auto &p : appliesChoices())
        if (p.first == applies)
            return p.second;
    return applies; // wildcards, as typed
}

bool ContextMenus::matches(const Item &item, const QStringList &paths, const std::function<bool(const QString &)> &isFolder)
{
    if (!item.enabled)
        return false;
    if (paths.isEmpty())
        return item.applies == QLatin1String("background");
    if (item.applies == QLatin1String("background") || (!item.several && paths.size() > 1))
        return false;
    FileKind::Category kind;
    const bool isKind = FileKind::categoryFromKey(item.applies, kind);
    QList<QRegularExpression> wild;
    if (!isKind && item.applies != QLatin1String("all") && item.applies != QLatin1String("files") && item.applies != QLatin1String("folders")) {
        for (const QString &w : item.applies.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            wild << QRegularExpression(QRegularExpression::wildcardToRegularExpression(w.trimmed()), QRegularExpression::CaseInsensitiveOption);
        if (wild.isEmpty())
            return false;
    }
    for (const QString &p : paths) {
        const bool folder = isFolder(p);
        const QString name = Location::fileName(p);
        if (item.applies == QLatin1String("all"))
            continue;
        if (item.applies == QLatin1String("files")) {
            if (folder)
                return false;
        } else if (item.applies == QLatin1String("folders")) {
            if (!folder)
                return false;
        } else if (isKind) {
            if (FileKind::categoryFor(name, folder) != kind)
                return false;
        } else {
            bool any = false;
            for (const QRegularExpression &rx : std::as_const(wild))
                any = any || rx.match(name).hasMatch();
            if (!any)
                return false;
        }
    }
    return true;
}
