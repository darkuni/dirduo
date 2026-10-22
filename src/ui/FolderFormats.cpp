#include "ui/FolderFormats.h"

#include "core/Location.h"
#include "core/Paths.h"

#include <QSettings>

FolderFormats &FolderFormats::instance()
{
    static FolderFormats f;
    return f;
}

FolderFormats::FolderFormats()
{
    reload();
}

QString FolderFormats::file() const
{
    return m_file.isEmpty() ? Paths::configFile(QStringLiteral("formats.ini")) : m_file;
}

void FolderFormats::setFile(const QString &f)
{
    m_file = f;
    reload();
}

namespace {

FolderFormat readFormat(QSettings &s)
{
    FolderFormat f;
    f.path = s.value(QStringLiteral("path")).toString();
    f.subfolders = s.value(QStringLiteral("subfolders"), false).toBool();
    f.header = QByteArray::fromBase64(s.value(QStringLiteral("header")).toByteArray());
    f.groupBy = s.value(QStringLiteral("groupBy")).toString();
    f.view = s.value(QStringLiteral("view")).toString();
    return f;
}

void writeFormat(QSettings &s, const FolderFormat &f)
{
    s.setValue(QStringLiteral("path"), f.path);
    s.setValue(QStringLiteral("subfolders"), f.subfolders);
    s.setValue(QStringLiteral("header"), f.header.toBase64());
    s.setValue(QStringLiteral("groupBy"), f.groupBy);
    s.setValue(QStringLiteral("view"), f.view);
}

} // namespace

void FolderFormats::reload()
{
    m_formats.clear();
    m_hasDefault = false;
    QSettings s(file(), QSettings::IniFormat);
    m_autoSwitch = s.value(QStringLiteral("General/autoSwitch"), true).toBool();
    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (g == QLatin1String("General"))
            continue;
        s.beginGroup(g);
        FolderFormat f = readFormat(s);
        s.endGroup();
        if (g == QLatin1String("Default")) {
            m_default = f;
            m_default.path.clear();
            m_hasDefault = true;
        } else if (!f.path.isEmpty()) {
            m_formats << f;
        }
    }
}

void FolderFormats::save() const
{
    QSettings s(file(), QSettings::IniFormat);
    s.clear();
    s.setValue(QStringLiteral("General/autoSwitch"), m_autoSwitch);
    if (m_hasDefault) {
        s.beginGroup(QStringLiteral("Default"));
        writeFormat(s, m_default);
        s.endGroup();
    }
    int n = 1;
    for (const FolderFormat &f : m_formats) {
        s.beginGroup(QStringLiteral("Folder%1").arg(n++));
        writeFormat(s, f);
        s.endGroup();
    }
    s.sync();
}

bool FolderFormats::find(const QString &rawPath, FolderFormat &out) const
{
    const QString path = Location::clean(rawPath);
    const FolderFormat *best = nullptr;
    for (const FolderFormat &f : m_formats) {
        if (f.path == path) {
            out = f;
            return true;
        }
        if (f.subfolders && Location::isSameOrInside(path, f.path)) {
            if (!best || f.path.size() > best->path.size())
                best = &f;
        }
    }
    if (best) {
        out = *best;
        return true;
    }
    return false;
}

bool FolderFormats::defaultFormat(FolderFormat &out) const
{
    if (!m_hasDefault)
        return false;
    out = m_default;
    return true;
}

void FolderFormats::set(const FolderFormat &format)
{
    FolderFormat f = format;
    f.path = Location::clean(f.path);
    for (FolderFormat &existing : m_formats) {
        if (existing.path == f.path) {
            existing = f;
            save();
            return;
        }
    }
    m_formats << f;
    save();
}

bool FolderFormats::remove(const QString &path)
{
    const QString p = Location::clean(path);
    for (int i = 0; i < m_formats.size(); ++i) {
        if (m_formats.at(i).path == p) {
            m_formats.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

void FolderFormats::setDefault(const FolderFormat &format)
{
    m_default = format;
    m_default.path.clear();
    m_hasDefault = true;
    save();
}

void FolderFormats::clearDefault()
{
    m_hasDefault = false;
    save();
}

void FolderFormats::setAutoSwitch(bool on)
{
    m_autoSwitch = on;
    save();
}
