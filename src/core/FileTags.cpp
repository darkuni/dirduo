#include "core/FileTags.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSaveFile>
#include <QSettings>

#include <atomic>
#include <cerrno>
#include <sys/xattr.h>

namespace {

const char *kTags = "user.xdg.tags";
const char *kRating = "user.baloo.rating";
const char *kComment = "user.xdg.comment";
const char *kLabel = "user.dirduo.label";
const char *kOldLabel = "user.linopus.label"; // before the rename: read, and cleared on the next write

QByteArray enc(const QString &p)
{
    return QFile::encodeName(p);
}

QByteArray getAttr(const QString &path, const char *name, bool *supported)
{
    QByteArray buf(4096, Qt::Uninitialized);
    const ssize_t n = ::getxattr(enc(path).constData(), name, buf.data(), size_t(buf.size()));
    if (n < 0) {
        if (supported && (errno == ENOTSUP))
            *supported = false;
        return {};
    }
    buf.truncate(int(n));
    return buf;
}

bool setAttr(const QString &path, const char *name, const QByteArray &value)
{
    if (value.isEmpty()) {
        if (::removexattr(enc(path).constData(), name) == 0 || errno == ENODATA)
            return true;
        return false;
    }
    return ::setxattr(enc(path).constData(), name, value.constData(), size_t(value.size()), 0) == 0;
}

// The fallback list: path -> data, in tags.json.
QMutex s_mutex;
std::atomic<bool> s_forceFallback{false};
QString s_fileOverride;
bool s_loaded = false;
QHash<QString, FileTagData> s_list;

QString listFile()
{
    return s_fileOverride.isEmpty() ? Paths::userDataHome() + QStringLiteral("/dirduo/tags.json") : s_fileOverride;
}

void loadList()
{
    if (s_loaded)
        return;
    s_loaded = true;
    s_list.clear();
    QFile f(listFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        s_list.insert(it.key(), FileTagData::fromMap(it.value().toObject().toVariantMap()));
}

void saveList()
{
    QJsonObject o;
    for (auto it = s_list.cbegin(); it != s_list.cend(); ++it)
        o.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
    QDir().mkpath(QFileInfo(listFile()).absolutePath());
    QSaveFile f(listFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QSettings settings()
{
    return QSettings(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
}

} // namespace

QVariantMap FileTagData::toMap() const
{
    QVariantMap m;
    if (!label.isEmpty())
        m.insert(QStringLiteral("label"), label);
    if (rating > 0)
        m.insert(QStringLiteral("rating"), rating);
    if (!tags.isEmpty())
        m.insert(QStringLiteral("tags"), tags);
    if (!comment.isEmpty())
        m.insert(QStringLiteral("comment"), comment);
    return m;
}

FileTagData FileTagData::fromMap(const QVariantMap &m)
{
    FileTagData d;
    d.label = m.value(QStringLiteral("label")).toString();
    d.rating = qBound(0, m.value(QStringLiteral("rating")).toInt(), 5);
    d.tags = m.value(QStringLiteral("tags")).toStringList();
    d.comment = m.value(QStringLiteral("comment")).toString();
    return d;
}

namespace FileTags {

FileTagData read(const QString &path)
{
    FileTagData d;
    // One call for most files: no attributes at all.
    char names[1024];
    ssize_t n = s_forceFallback ? 0 : ::listxattr(enc(path).constData(), names, sizeof(names));
    bool supported = !(n < 0 && errno == ENOTSUP);
    if (n == 0) {
        QMutexLocker lock(&s_mutex);
        loadList();
        if (s_list.isEmpty())
            return d;
        return s_list.value(QFileInfo(path).absoluteFilePath());
    }
    const QByteArray tags = supported ? getAttr(path, kTags, &supported) : QByteArray();
    if (supported) {
        for (const QString &t : QString::fromUtf8(tags).split(QLatin1Char(','), Qt::SkipEmptyParts))
            d.tags << t.trimmed();
        d.rating = qBound(0, (QString::fromLatin1(getAttr(path, kRating, nullptr)).toInt() + 1) / 2, 5);
        d.comment = QString::fromUtf8(getAttr(path, kComment, nullptr));
        d.label = QString::fromUtf8(getAttr(path, kLabel, nullptr));
        if (d.label.isEmpty())
            d.label = QString::fromUtf8(getAttr(path, kOldLabel, nullptr));
        if (!d.isEmpty())
            return d;
    }
    QMutexLocker lock(&s_mutex);
    loadList();
    return s_list.value(QFileInfo(path).absoluteFilePath());
}

bool storedOnFile(const QString &path)
{
    if (s_forceFallback)
        return false;
    bool supported = true;
    getAttr(path, kTags, &supported);
    return supported && QFileInfo::exists(path);
}

bool write(const QString &path, const FileTagData &data, QString *error)
{
    const QString key = QFileInfo(path).absoluteFilePath();
    bool onFile = !s_forceFallback && setAttr(path, kTags, data.tags.join(QLatin1Char(',')).toUtf8())
        && setAttr(path, kRating, data.rating > 0 ? QByteArray::number(data.rating * 2) : QByteArray())
        && setAttr(path, kComment, data.comment.toUtf8()) && setAttr(path, kLabel, data.label.toUtf8());
    if (onFile)
        setAttr(path, kOldLabel, QByteArray()); // the old name's label goes
    QMutexLocker lock(&s_mutex);
    loadList();
    if (onFile) {
        if (s_list.remove(key) > 0)
            saveList();
    } else {
        // FAT, exFAT, a read-only place: DirDuo's own list.
        if (!QFileInfo::exists(path)) {
            if (error)
                *error = QStringLiteral("%1 is not there.").arg(QFileInfo(path).fileName());
            return false;
        }
        if (data.isEmpty())
            s_list.remove(key);
        else
            s_list.insert(key, data);
        saveList();
    }
    lock.unlock();
    noteTags(data.tags);
    return true;
}

void renamed(const QString &from, const QString &to)
{
    QMutexLocker lock(&s_mutex);
    loadList();
    const QString f = QFileInfo(from).absoluteFilePath();
    const QString t = QFileInfo(to).absoluteFilePath();
    bool changed = false;
    const QList<QString> keys = s_list.keys();
    for (const QString &k : keys) {
        if (k == f || k.startsWith(f + QLatin1Char('/'))) {
            s_list.insert(t + k.mid(f.size()), s_list.take(k));
            changed = true;
        }
    }
    if (changed)
        saveList();
}

void copyAttributes(const QString &from, const QString &to)
{
    QByteArray names(4096, Qt::Uninitialized);
    const ssize_t n = ::listxattr(enc(from).constData(), names.data(), size_t(names.size()));
    if (n <= 0)
        return;
    names.truncate(int(n));
    for (const QByteArray &name : names.split('\0')) {
        if (!name.startsWith("user."))
            continue;
        const QByteArray v = getAttr(from, name.constData(), nullptr);
        ::setxattr(enc(to).constData(), name.constData(), v.constData(), size_t(v.size()), 0);
    }
}

QList<LabelDef> labels()
{
    QSettings s = settings();
    const int n = s.beginReadArray(QStringLiteral("ColorLabels"));
    QList<LabelDef> out;
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        LabelDef d{s.value(QStringLiteral("name")).toString(), QColor(s.value(QStringLiteral("color")).toString())};
        if (!d.name.isEmpty() && d.color.isValid())
            out << d;
    }
    s.endArray();
    if (n > 0)
        return out;
    return {{QStringLiteral("Red"), QColor(0xe5, 0x39, 0x35)},    {QStringLiteral("Orange"), QColor(0xfb, 0x8c, 0x00)},
            {QStringLiteral("Yellow"), QColor(0xfd, 0xd8, 0x35)}, {QStringLiteral("Green"), QColor(0x43, 0xa0, 0x47)},
            {QStringLiteral("Blue"), QColor(0x1e, 0x88, 0xe5)},   {QStringLiteral("Purple"), QColor(0x8e, 0x24, 0xaa)},
            {QStringLiteral("Gray"), QColor(0x75, 0x75, 0x75)}};
}

void setLabels(const QList<LabelDef> &list)
{
    QSettings s = settings();
    s.remove(QStringLiteral("ColorLabels"));
    s.beginWriteArray(QStringLiteral("ColorLabels"), int(list.size()));
    for (int i = 0; i < list.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), list.at(i).name);
        s.setValue(QStringLiteral("color"), list.at(i).color.name());
    }
    s.endArray();
}

QColor colorOf(const QString &label)
{
    if (label.isEmpty())
        return {};
    for (const LabelDef &d : labels()) {
        if (d.name.compare(label, Qt::CaseInsensitive) == 0)
            return d.color;
    }
    return {};
}

QStringList knownTags()
{
    QSettings s = settings();
    return s.value(QStringLiteral("Tags/known")).toStringList();
}

void noteTags(const QStringList &tags)
{
    if (tags.isEmpty())
        return;
    QSettings s = settings();
    QStringList known = s.value(QStringLiteral("Tags/known")).toStringList();
    for (const QString &t : tags) {
        known.removeAll(t);
        known.prepend(t);
    }
    while (known.size() > 200)
        known.removeLast();
    s.setValue(QStringLiteral("Tags/known"), known);
}

QString stars(int rating)
{
    if (rating <= 0)
        return {};
    return QString(qBound(0, rating, 5), QChar(0x2605)) + QString(5 - qBound(0, rating, 5), QChar(0x2606));
}

void setForceFallback(bool on)
{
    s_forceFallback = on;
}

void setFallbackFile(const QString &file)
{
    QMutexLocker lock(&s_mutex);
    s_fileOverride = file;
    s_loaded = false;
    s_list.clear();
}

} // namespace FileTags
