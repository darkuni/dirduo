#include "core/KeyMap.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSaveFile>

KeyMap *KeyMap::instance()
{
    static KeyMap *k = new KeyMap;
    return k;
}

KeyMap::KeyMap()
{
    reload();
}

QString KeyMap::file() const
{
    return m_file.isEmpty() ? Paths::configFile(QStringLiteral("keys.json")) : m_file;
}

void KeyMap::setFile(const QString &file)
{
    m_file = file;
    reload();
    Q_EMIT changed();
}

namespace {

QJsonArray hotkeysToJson(const QList<KeyMap::Hotkey> &list)
{
    QJsonArray a;
    for (const KeyMap::Hotkey &h : list) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), h.name);
        o.insert(QStringLiteral("key"), h.key);
        o.insert(QStringLiteral("command"), h.command);
        if (!h.enabled)
            o.insert(QStringLiteral("enabled"), false);
        a.append(o);
    }
    return a;
}

QList<KeyMap::Hotkey> hotkeysFromJson(const QJsonArray &a)
{
    QList<KeyMap::Hotkey> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        KeyMap::Hotkey h;
        h.name = o.value(QStringLiteral("name")).toString();
        h.key = o.value(QStringLiteral("key")).toString();
        h.command = o.value(QStringLiteral("command")).toString();
        h.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (!h.command.trimmed().isEmpty())
            out << h;
    }
    return out;
}

} // namespace

void KeyMap::reload()
{
    m_overrides.clear();
    m_hotkeys.clear();
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject menus = root.value(QStringLiteral("menus")).toObject();
    for (auto it = menus.begin(); it != menus.end(); ++it) {
        QStringList keys;
        for (const QJsonValue &k : it.value().toArray())
            if (!k.toString().isEmpty())
                keys << k.toString();
        m_overrides.insert(it.key(), keys);
    }
    m_hotkeys = hotkeysFromJson(root.value(QStringLiteral("hotkeys")).toArray());
}

bool KeyMap::save(QString *error)
{
    QJsonObject menus;
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        menus.insert(it.key(), QJsonArray::fromStringList(it.value()));
    QJsonObject root;
    root.insert(QStringLiteral("menus"), menus);
    root.insert(QStringLiteral("hotkeys"), hotkeysToJson(m_hotkeys));
    QDir().mkpath(QFileInfo(file()).absolutePath());
    QSaveFile f(file());
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    return f.commit();
}

void KeyMap::registerMenuItem(const MenuItem &item)
{
    for (const MenuItem &m : std::as_const(m_items))
        if (m.id == item.id)
            return; // the first lister's defaults stand
    m_items << item;
}

KeyMap::MenuItem KeyMap::menuItem(const QString &id) const
{
    for (const MenuItem &m : m_items)
        if (m.id == id)
            return m;
    return {};
}

QStringList KeyMap::keysFor(const QString &id) const
{
    const auto it = m_overrides.constFind(id);
    if (it != m_overrides.constEnd())
        return it.value();
    return menuItem(id).defaults;
}

void KeyMap::set(const QHash<QString, QStringList> &overrides, const QList<Hotkey> &hotkeys)
{
    m_overrides.clear();
    // Only what differs from the defaults is kept.
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        const MenuItem m = menuItem(it.key());
        if (m.id.isEmpty() || it.value() != m.defaults)
            m_overrides.insert(it.key(), it.value());
    }
    m_hotkeys = hotkeys;
    save();
    Q_EMIT changed();
}

bool KeyMap::exportHotkeys(const QString &file, const QList<Hotkey> &hotkeys, QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("dirduoHotkeys"), 1);
    root.insert(QStringLiteral("hotkeys"), hotkeysToJson(hotkeys));
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

QList<KeyMap::Hotkey> KeyMap::importHotkeys(const QString &file, bool *ok, QString *error)
{
    if (ok)
        *ok = false;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject() || !doc.object().contains(QStringLiteral("hotkeys"))) {
        if (error)
            *error = QStringLiteral("This is not a file of DirDuo hotkeys.");
        return {};
    }
    if (ok)
        *ok = true;
    return hotkeysFromJson(doc.object().value(QStringLiteral("hotkeys")).toArray());
}

QString KeyMap::display(const QString &portable)
{
    return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QString KeyMap::portable(const QKeySequence &seq)
{
    return seq.toString(QKeySequence::PortableText);
}
