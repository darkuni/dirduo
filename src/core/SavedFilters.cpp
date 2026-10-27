#include "core/SavedFilters.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

SavedFilters *SavedFilters::instance()
{
    static SavedFilters *s = new SavedFilters;
    return s;
}

SavedFilters::SavedFilters()
{
    reload();
}

QString SavedFilters::file() const
{
    return Paths::configFile(QStringLiteral("filters.json"));
}

QList<SavedFilters::Filter> SavedFilters::defaults()
{
    auto f = [](const char *name, const char *pattern) {
        Filter x;
        x.name = QString::fromUtf8(name);
        x.pattern = QString::fromUtf8(pattern);
        return x;
    };
    return {
        f("Pictures", "*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp;*.tif;*.tiff;*.heic;*.avif"),
        f("Music", "*.mp3;*.flac;*.ogg;*.opus;*.m4a;*.aac;*.wav;*.aiff;*.wma;*.sid"),
        f("Video", "*.mkv;*.mp4;*.m4v;*.avi;*.mov;*.webm;*.wmv;*.mpg;*.mpeg;*.ts"),
        f("Disc images", "*.iso;*.chd;*.cue;*.bin;*.img;*.rvz;*.wbfs;*.cso;*.pbp;*.gdi;*.cdi"),
        f("Archives", "*.zip;*.7z;*.rar;*.tar;*.gz;*.tgz;*.xz;*.zst;*.bz2"),
        f("Documents", "*.pdf;*.txt;*.md;*.doc;*.docx;*.odt;*.rtf;*.epub;*.nfo"),
    };
}

void SavedFilters::reload()
{
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly)) {
        m_filters = defaults();
        return;
    }
    m_filters.clear();
    const QJsonArray a = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("filters")).toArray();
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        Filter x;
        x.name = o.value(QStringLiteral("name")).toString().trimmed();
        x.pattern = o.value(QStringLiteral("pattern")).toString();
        x.hide = o.value(QStringLiteral("hide")).toBool();
        x.scope = o.value(QStringLiteral("scope")).toString(QStringLiteral("files"));
        if (!x.name.isEmpty())
            m_filters << x;
    }
}

void SavedFilters::write()
{
    QJsonArray a;
    for (const Filter &x : std::as_const(m_filters)) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), x.name);
        o.insert(QStringLiteral("pattern"), x.pattern);
        if (x.hide)
            o.insert(QStringLiteral("hide"), true);
        if (x.scope != QLatin1String("files"))
            o.insert(QStringLiteral("scope"), x.scope);
        a.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("filters"), a);
    QDir().mkpath(QFileInfo(file()).absolutePath());
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
}

bool SavedFilters::has(const QString &name) const
{
    return !filter(name).name.isEmpty();
}

SavedFilters::Filter SavedFilters::filter(const QString &name) const
{
    for (const Filter &x : m_filters)
        if (x.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return x;
    return {};
}

void SavedFilters::setFilters(const QList<Filter> &filters)
{
    m_filters = filters;
    write();
    Q_EMIT changed();
}

void SavedFilters::save(const Filter &f)
{
    for (Filter &x : m_filters) {
        if (x.name.compare(f.name, Qt::CaseInsensitive) == 0) {
            x = f;
            write();
            Q_EMIT changed();
            return;
        }
    }
    m_filters << f;
    write();
    Q_EMIT changed();
}

bool SavedFilters::remove(const QString &name)
{
    for (int i = 0; i < m_filters.size(); ++i) {
        if (m_filters.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            m_filters.removeAt(i);
            write();
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

QString SavedFilters::resolve(const QString &text, bool *hide) const
{
    if (hide)
        *hide = false;
    if (!text.startsWith(QLatin1Char('@')))
        return text;
    const Filter f = filter(text.mid(1));
    if (f.name.isEmpty())
        return text;
    if (hide)
        *hide = f.hide;
    return f.pattern;
}
