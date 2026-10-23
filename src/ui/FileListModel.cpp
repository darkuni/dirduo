#include "ui/FileListModel.h"

#include "core/FileKind.h"
#include "core/FileOps.h"
#include "core/FolderSizes.h"
#include "core/Location.h"
#include "core/Metadata.h"
#include "core/Thumbnails.h"
#include "ui/Style.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSet>
#include <QStyle>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <utility>

FileListModel::FileListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    connect(FolderSizes::instance(), &FolderSizes::ready, this, &FileListModel::onFolderSizeReady);
    connect(MetadataCache::instance(), &MetadataCache::ready, this, &FileListModel::onMetadataReady);
    connect(ThumbnailCache::instance(), &ThumbnailCache::ready, this, &FileListModel::onThumbnailReady);
    connect(Style::instance(), &Style::changed, this, [this]() {
        if (!m_items.isEmpty())
            Q_EMIT dataChanged(index(0, 0), index(m_items.size() - 1, ColumnCount - 1));
    });
}

QString FileListModel::columnName(int column)
{
    switch (column) {
    case NameColumn:
        return QStringLiteral("Name");
    case SizeColumn:
        return QStringLiteral("Size");
    case TypeColumn:
        return QStringLiteral("Type");
    case ModifiedColumn:
        return QStringLiteral("Modified");
    case CreatedColumn:
        return QStringLiteral("Created");
    case AccessedColumn:
        return QStringLiteral("Accessed");
    case ExtensionColumn:
        return QStringLiteral("Extension");
    case PermissionsColumn:
        return QStringLiteral("Permissions");
    case OwnerColumn:
        return QStringLiteral("Owner");
    case GroupColumn:
        return QStringLiteral("Group");
    case LocationColumn:
        return QStringLiteral("Location");
    case TitleColumn:
        return QStringLiteral("Title");
    case ArtistColumn:
        return QStringLiteral("Artist");
    case AlbumColumn:
        return QStringLiteral("Album");
    case TrackColumn:
        return QStringLiteral("Track");
    case YearColumn:
        return QStringLiteral("Year");
    case GenreColumn:
        return QStringLiteral("Genre");
    case DurationColumn:
        return QStringLiteral("Length");
    case BitrateColumn:
        return QStringLiteral("Bitrate");
    case DimensionsColumn:
        return QStringLiteral("Dimensions");
    case TakenColumn:
        return QStringLiteral("Date Taken");
    case CameraColumn:
        return QStringLiteral("Camera");
    case RatingColumn:
        return QStringLiteral("Rating");
    case LabelColumn:
        return QStringLiteral("Label");
    case TagsColumn:
        return QStringLiteral("Tags");
    case DescriptionColumn:
        return QStringLiteral("Description");
    }
    return {};
}

QString FileListModel::groupByLabel(GroupBy g)
{
    if (g == GroupBy::None)
        return QStringLiteral("No Grouping");
    if (g == GroupBy::Taken)
        return QStringLiteral("Date Taken");
    QString label = groupByName(g);
    label[0] = label[0].toUpper();
    return label;
}

QList<FileListModel::GroupBy> FileListModel::allGroupings()
{
    return {GroupBy::None,   GroupBy::Type,   GroupBy::Name,  GroupBy::Size,   GroupBy::Modified,
            GroupBy::Extension, GroupBy::Folder, GroupBy::Label, GroupBy::Rating, GroupBy::Artist,
            GroupBy::Album,  GroupBy::Genre,  GroupBy::Year,  GroupBy::Camera, GroupBy::Taken};
}

QString FileListModel::groupByName(GroupBy g)
{
    switch (g) {
    case GroupBy::None:
        return QStringLiteral("none");
    case GroupBy::Type:
        return QStringLiteral("type");
    case GroupBy::Name:
        return QStringLiteral("name");
    case GroupBy::Size:
        return QStringLiteral("size");
    case GroupBy::Modified:
        return QStringLiteral("modified");
    case GroupBy::Extension:
        return QStringLiteral("extension");
    case GroupBy::Folder:
        return QStringLiteral("folder");
    case GroupBy::Label:
        return QStringLiteral("label");
    case GroupBy::Rating:
        return QStringLiteral("rating");
    case GroupBy::Artist:
        return QStringLiteral("artist");
    case GroupBy::Album:
        return QStringLiteral("album");
    case GroupBy::Genre:
        return QStringLiteral("genre");
    case GroupBy::Year:
        return QStringLiteral("year");
    case GroupBy::Camera:
        return QStringLiteral("camera");
    case GroupBy::Taken:
        return QStringLiteral("taken");
    }
    return QStringLiteral("none");
}

bool FileListModel::groupByFromName(const QString &name, GroupBy &out)
{
    const QString n = name.toLower();
    for (GroupBy g : allGroupings()) {
        if (groupByName(g) == n) {
            out = g;
            return true;
        }
    }
    if (n == QLatin1String("off") || n.isEmpty()) {
        out = GroupBy::None;
        return true;
    }
    if (n == QLatin1String("date")) {
        out = GroupBy::Modified;
        return true;
    }
    return false;
}

void FileListModel::setGroupBy(GroupBy g)
{
    if (m_groupBy == g)
        return;
    m_groupBy = g;
    if (!m_items.isEmpty())
        Q_EMIT dataChanged(index(0, 0), index(m_items.size() - 1, ColumnCount - 1), {GroupKeyRole, GroupLabelRole});
}

void FileListModel::setLiveFolderSizes(bool on)
{
    if (m_liveSizes == on)
        return;
    m_liveSizes = on;
    if (on)
        requestAllFolderSizes();
    if (!m_items.isEmpty())
        Q_EMIT dataChanged(index(0, SizeColumn), index(m_items.size() - 1, SizeColumn));
}

void FileListModel::requestFolderSize(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    const FileItem &it = m_items.at(row);
    if (it.isDir && !it.isSymlink && Location::isLocal(it.path))
        FolderSizes::instance()->request(it.path);
}

void FileListModel::requestAllFolderSizes()
{
    m_maxFolderBytes = 0;
    for (int row = 0; row < m_items.size(); ++row) {
        const FileItem &it = m_items.at(row);
        if (!it.isDir)
            continue;
        if (FolderSizes::instance()->has(it.path))
            m_maxFolderBytes = qMax(m_maxFolderBytes, FolderSizes::instance()->result(it.path).bytes);
        else
            requestFolderSize(row);
    }
}

void FileListModel::onFolderSizeReady(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const qint64 bytes = FolderSizes::instance()->result(path).bytes;
    if (bytes > m_maxFolderBytes) {
        m_maxFolderBytes = bytes;
        // Bars are relative to the biggest folder: they all change.
        if (m_liveSizes) {
            Q_EMIT dataChanged(index(0, SizeColumn), index(m_items.size() - 1, SizeColumn));
            return;
        }
    }
    Q_EMIT dataChanged(index(row, SizeColumn), index(row, SizeColumn));
}

QString FileListModel::folderSizeText(int row) const
{
    const FileItem &it = m_items.at(row);
    FolderSizes *sizes = FolderSizes::instance();
    if (sizes->has(it.path)) {
        const FolderSizes::Result r = sizes->result(it.path);
        if (r.isEmpty())
            return QStringLiteral("EMPTY");
        return FileOps::formatSize(r.bytes);
    }
    if (sizes->isPending(it.path))
        return QStringLiteral("\u2026");
    return {};
}

QPair<QString, QString> FileListModel::groupFor(int row) const
{
    if (m_items.at(row).depth > 0) {
        // Inside an expanded folder: in that folder's group.
        const QList<int> c = chain(row);
        if (!c.isEmpty() && c.first() != row)
            return groupFor(c.first());
    }
    const FileItem &it = m_items.at(row);
    switch (m_groupBy) {
    case GroupBy::None:
        return {};
    case GroupBy::Type: {
        const auto c = FileKind::categoryFor(it.name, it.isDir);
        return {QStringLiteral("%1").arg(static_cast<int>(c), 2, 10, QLatin1Char('0')), FileKind::categoryName(c)};
    }
    case GroupBy::Name: {
        const QChar ch = it.name.isEmpty() ? QChar() : it.name.at(0).toUpper();
        if (ch.isLetter())
            return {QStringLiteral("2") + ch, QString(ch)};
        if (ch.isDigit())
            return {QStringLiteral("1"), QStringLiteral("0-9")};
        return {QStringLiteral("0"), QStringLiteral("#")};
    }
    case GroupBy::Size: {
        if (it.isDir)
            return {QStringLiteral("0"), QStringLiteral("Folders")};
        const qint64 s = it.size;
        if (s == 0)
            return {QStringLiteral("1"), QStringLiteral("Empty")};
        if (s < 16 * 1024)
            return {QStringLiteral("2"), QStringLiteral("Tiny (under 16 KB)")};
        if (s < 1024 * 1024)
            return {QStringLiteral("3"), QStringLiteral("Small (16 KB to 1 MB)")};
        if (s < 128LL * 1024 * 1024)
            return {QStringLiteral("4"), QStringLiteral("Medium (1 to 128 MB)")};
        if (s < 1024LL * 1024 * 1024)
            return {QStringLiteral("5"), QStringLiteral("Large (128 MB to 1 GB)")};
        if (s < 16LL * 1024 * 1024 * 1024)
            return {QStringLiteral("6"), QStringLiteral("Huge (1 to 16 GB)")};
        return {QStringLiteral("7"), QStringLiteral("Gigantic (over 16 GB)")};
    }
    case GroupBy::Modified: {
        const QDate d = it.modified.date();
        const QDate today = QDate::currentDate();
        const int days = d.daysTo(today);
        const QDate weekStart = today.addDays(1 - today.dayOfWeek());
        if (days < 0) // copied from a machine with its clock ahead
            return {QStringLiteral("10"), QStringLiteral("In the future")};
        if (days == 0)
            return {QStringLiteral("11"), QStringLiteral("Today")};
        if (days == 1)
            return {QStringLiteral("12"), QStringLiteral("Yesterday")};
        if (d >= weekStart)
            return {QStringLiteral("13"), QStringLiteral("Earlier this week")};
        if (d >= weekStart.addDays(-7))
            return {QStringLiteral("14"), QStringLiteral("Last week")};
        if (d.year() == today.year() && d.month() == today.month())
            return {QStringLiteral("15"), QStringLiteral("Earlier this month")};
        const QDate lastMonth = today.addMonths(-1);
        if (d.year() == lastMonth.year() && d.month() == lastMonth.month())
            return {QStringLiteral("16"), QStringLiteral("Last month")};
        if (d.year() == today.year())
            return {QStringLiteral("17"), QStringLiteral("Earlier this year")};
        if (d.year() == today.year() - 1)
            return {QStringLiteral("18"), QStringLiteral("Last year")};
        return {QStringLiteral("19"), QStringLiteral("Older")};
    }
    case GroupBy::Extension: {
        if (it.isDir)
            return {QStringLiteral("0"), QStringLiteral("Folders")};
        const QString ext = FileKind::extensionOf(it.name);
        if (ext.isEmpty())
            return {QStringLiteral("1"), QStringLiteral("No extension")};
        return {QStringLiteral("2") + ext, ext.toUpper()};
    }
    case GroupBy::Folder:
        return {it.relPath, it.relPath.isEmpty() ? QStringLiteral("(this folder)") : it.relPath};
    case GroupBy::Label: {
        if (it.tags.label.isEmpty())
            return {QStringLiteral("9"), QStringLiteral("No label")};
        // In the order the labels are defined.
        const QList<LabelDef> defs = FileTags::labels();
        int at = 0;
        for (; at < defs.size(); ++at) {
            if (defs.at(at).name.compare(it.tags.label, Qt::CaseInsensitive) == 0)
                break;
        }
        return {QStringLiteral("1:%1").arg(at, 3, 10, QLatin1Char('0')), it.tags.label};
    }
    case GroupBy::Rating:
        if (it.tags.rating <= 0)
            return {QStringLiteral("9"), QStringLiteral("Not rated")};
        return {QStringLiteral("%1").arg(5 - it.tags.rating), FileTags::stars(it.tags.rating)};
    case GroupBy::Artist:
    case GroupBy::Album:
    case GroupBy::Genre:
    case GroupBy::Year:
    case GroupBy::Camera:
    case GroupBy::Taken: {
        if (it.isDir)
            return {QStringLiteral("0"), QStringLiteral("Folders")};
        static const QHash<GroupBy, QString> none = {
            {GroupBy::Artist, QStringLiteral("No artist")}, {GroupBy::Album, QStringLiteral("No album")},
            {GroupBy::Genre, QStringLiteral("No genre")},   {GroupBy::Year, QStringLiteral("No year")},
            {GroupBy::Camera, QStringLiteral("No camera")}, {GroupBy::Taken, QStringLiteral("No date taken")}};
        const QPair<QString, QString> nothing{QStringLiteral("9"), none.value(m_groupBy)};
        MediaInfo info;
        if (Metadata::kindFor(it.name) == MediaInfo::Kind::None)
            return nothing;
        if (!mediaInfo(row, &info))
            return {QStringLiteral("8"), QStringLiteral("Reading\u2026")};
        auto named = [&nothing](const QString &value) -> QPair<QString, QString> {
            if (value.isEmpty())
                return nothing;
            return {QStringLiteral("5:") + value.toLower(), value};
        };
        switch (m_groupBy) {
        case GroupBy::Artist:
            return named(info.albumArtist.isEmpty() ? info.artist : info.albumArtist);
        case GroupBy::Album:
            return named(info.album);
        case GroupBy::Genre:
            return named(info.genre);
        case GroupBy::Year:
            return info.year > 0 ? QPair<QString, QString>{QStringLiteral("5:%1").arg(info.year), QString::number(info.year)} : nothing;
        case GroupBy::Camera:
            return named(info.camera);
        case GroupBy::Taken:
            if (!info.taken.isValid())
                return nothing;
            return {QStringLiteral("5:") + info.taken.toString(QStringLiteral("yyyy-MM")),
                    QLocale().standaloneMonthName(info.taken.date().month()) + QLatin1Char(' ')
                        + QString::number(info.taken.date().year())};
        default:
            return nothing;
        }
    }
    }
    return {};
}

void FileListModel::setThumbnails(bool on)
{
    if (m_thumbs == on)
        return;
    m_thumbs = on;
    if (!m_items.isEmpty())
        Q_EMIT dataChanged(index(0, NameColumn), index(m_items.size() - 1, NameColumn), {Qt::DecorationRole});
}

QVariant FileListModel::thumbnailFor(int row) const
{
    const FileItem &it = m_items.at(row);
    if (it.isDir || !Location::isLocal(it.path) || it.path.startsWith(QLatin1String("coll:")))
        return {};
    if (auto cached = m_thumbIcons.constFind(it.path); cached != m_thumbIcons.cend())
        return cached->isNull() ? QVariant() : QVariant(*cached);
    QImage img;
    if (!ThumbnailCache::instance()->lookup(it.path, it.modified, &img))
        return {}; // on its way; the plain icon meanwhile
    if (img.isNull()) {
        m_thumbIcons.insert(it.path, QIcon());
        return {};
    }
    // On a square, so a row of thumbnails lines up whatever their shape, with
    // a thin edge and a soft shadow.
    const int side = Thumbnails::kLarge;
    QSize fit = img.size().scaled(side - 12, side - 12, Qt::KeepAspectRatio);
    if (img.width() < fit.width() && img.height() < fit.height())
        fit = img.size(); // small pictures stay small
    QImage canvas(side, side, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r(QPoint((side - fit.width()) / 2, (side - fit.height()) / 2), fit);
    for (int i = 4; i > 0; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 14));
        p.drawRoundedRect(QRectF(r).adjusted(-i, -i + 3, i, i + 3), 3 + i, 3 + i);
    }
    p.drawImage(r, img);
    p.setPen(QPen(QColor(0, 0, 0, 70), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
    p.end();
    const QIcon icon(QPixmap::fromImage(canvas));
    m_thumbIcons.insert(it.path, icon);
    return icon;
}

void FileListModel::onThumbnailReady(const QString &path)
{
    if (!m_thumbs)
        return;
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    m_thumbIcons.remove(path);
    m_thumbRows.insert(row);
    if (m_thumbPending)
        return;
    m_thumbPending = true;
    QTimer::singleShot(80, this, [this]() {
        m_thumbPending = false;
        const QSet<int> rows = std::exchange(m_thumbRows, {});
        for (int r : rows) {
            if (r < m_items.size())
                Q_EMIT dataChanged(index(r, NameColumn), index(r, NameColumn), {Qt::DecorationRole});
        }
    });
}

bool FileListModel::mediaInfo(int row, MediaInfo *out) const
{
    if (row < 0 || row >= m_items.size())
        return false;
    const FileItem &it = m_items.at(row);
    if (it.isDir || !Location::isLocal(it.path) || Metadata::kindFor(it.name) == MediaInfo::Kind::None)
        return false;
    return MetadataCache::instance()->lookup(it.path, it.size, it.modified, out);
}

void FileListModel::onMetadataReady(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    m_metaRows.insert(row);
    if (m_metaPending)
        return;
    // A folder of a thousand songs comes in a few at a time; one update per
    // batch keeps the list from re-sorting a thousand times.
    m_metaPending = true;
    QTimer::singleShot(120, this, [this]() {
        m_metaPending = false;
        const QSet<int> rows = std::exchange(m_metaRows, {});
        if (rows.isEmpty())
            return;
        int lo = INT_MAX, hi = -1;
        for (int r : rows) {
            if (r < m_items.size()) {
                lo = qMin(lo, r);
                hi = qMax(hi, r);
            }
        }
        if (hi >= 0)
            Q_EMIT dataChanged(index(lo, 0), index(hi, ColumnCount - 1));
    });
}

QVariant FileListModel::metadataValue(int row, int column, bool forSorting) const
{
    MediaInfo info;
    if (!mediaInfo(row, &info))
        return forSorting ? QVariant(QString()) : QVariant();
    auto number = [forSorting](int v) -> QVariant {
        if (forSorting)
            return v;
        return v > 0 ? QVariant(QString::number(v)) : QVariant();
    };
    switch (column) {
    case TitleColumn:
        return info.title;
    case ArtistColumn:
        return info.artist;
    case AlbumColumn:
        return info.album;
    case TrackColumn:
        return number(info.track);
    case YearColumn:
        return number(info.year);
    case GenreColumn:
        return info.genre;
    case DurationColumn:
        return forSorting ? QVariant(info.seconds) : QVariant(info.durationText());
    case BitrateColumn:
        if (forSorting)
            return info.bitrate;
        return info.bitrate > 0 ? QVariant(QStringLiteral("%1 kbit/s").arg(info.bitrate)) : QVariant();
    case DimensionsColumn:
        return forSorting ? QVariant(qint64(info.width) * info.height) : QVariant(info.dimensionsText());
    case TakenColumn:
        return forSorting ? QVariant(info.taken) : QVariant(info.taken.isValid() ? QLocale().toString(info.taken, QLocale::ShortFormat) : QString());
    case CameraColumn:
        return info.camera;
    }
    return {};
}

void FileListModel::rebuildIndex()
{
    m_rowByPath.clear();
    m_rowByPath.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i)
        m_rowByPath.insert(m_items.at(i).path, i);
}

void FileListModel::setListing(const QString &dir, const FileItemList &items)
{
    beginResetModel();
    m_dir = dir;
    m_items = items;
    m_mimeNames = QVector<QString>(m_items.size());
    m_thumbIcons.clear();
    m_expanded.clear();
    rebuildIndex();
    endResetModel();
    if (m_liveSizes)
        requestAllFolderSizes();
}

void FileListModel::clear(const QString &dir)
{
    setListing(dir, {});
}

void FileListModel::updateListing(const FileItemList &items)
{
    QHash<QString, int> incoming;
    incoming.reserve(items.size());
    for (int i = 0; i < items.size(); ++i)
        incoming.insert(items.at(i).path, i);

    // A wholesale change (a folder that was emptied and refilled) is cheaper
    // as a reset; not with folders expanded, whose contents would go too.
    int kept = 0;
    int top = 0;
    for (const FileItem &it : std::as_const(m_items)) {
        if (it.depth > 0)
            continue;
        ++top;
        kept += incoming.contains(it.path) ? 1 : 0;
    }
    if (m_expanded.isEmpty() && top > 50 && kept < top / 2) {
        setListing(m_dir, items);
        return;
    }

    // Removals: top-level items that went, and whatever was under them.
    QSet<int> gone;
    QSet<QString> goneFolders;
    for (int row = 0; row < m_items.size(); ++row) {
        const FileItem &it = m_items.at(row);
        if (it.depth == 0 && !incoming.contains(it.path)) {
            gone.insert(row);
            if (it.isDir)
                goneFolders.insert(it.path);
        }
    }
    gone += descendantRows(goneFolders);
    for (const QString &f : std::as_const(goneFolders))
        m_expanded.remove(f);
    dropRows(gone);

    // Changes in place.
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).depth > 0)
            continue;
        const FileItem &fresh = items.at(incoming.value(m_items.at(row).path));
        if (!m_items.at(row).sameContentAs(fresh)) {
            const bool kindChanged = m_items.at(row).isDir != fresh.isDir || m_items.at(row).name != fresh.name;
            m_items[row] = fresh;
            m_thumbIcons.remove(fresh.path);
            if (kindChanged)
                m_mimeNames[row].clear();
            Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
        }
    }

    // Additions at the end; the proxy sorts them into place.
    FileItemList added;
    for (const FileItem &it : items) {
        if (!m_rowByPath.contains(it.path))
            added.append(it);
    }
    if (!added.isEmpty()) {
        const int first = m_items.size();
        beginInsertRows({}, first, first + added.size() - 1);
        m_items += added;
        m_mimeNames.resize(m_items.size());
        rebuildIndex();
        endInsertRows();
        if (m_liveSizes)
            requestAllFolderSizes();
    }
}

// ---- Expandable folders -----------------------------------------------------

void FileListModel::setExpandable(bool on)
{
    if (m_expandable == on)
        return;
    m_expandable = on;
    if (!m_items.isEmpty())
        Q_EMIT dataChanged(index(0, NameColumn), index(m_items.size() - 1, NameColumn), {ExpandRole});
}

bool FileListModel::canExpand(int row) const
{
    if (!m_expandable || row < 0 || row >= m_items.size())
        return false;
    const FileItem &it = m_items.at(row);
    return it.isDir && !it.path.startsWith(QLatin1String("coll:"));
}

void FileListModel::setExpanded(const QString &path, bool expanded)
{
    if (expanded)
        m_expanded.insert(path);
    else
        m_expanded.remove(path);
    const int row = rowForPath(path);
    if (row >= 0)
        Q_EMIT dataChanged(index(row, NameColumn), index(row, NameColumn), {ExpandRole});
}

int FileListModel::parentRow(int row) const
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).depth == 0)
        return -1;
    return rowForPath(m_items.at(row).parentPath);
}

QList<int> FileListModel::chain(int row) const
{
    QList<int> out;
    for (int r = row; r >= 0 && out.size() < 64; r = parentRow(r))
        out.prepend(r);
    return out;
}

QSet<int> FileListModel::descendantRows(const QSet<QString> &parents) const
{
    QSet<int> out;
    if (parents.isEmpty())
        return out;
    QSet<QString> folders = parents;
    // Children were appended after their parents, but a refresh can append
    // more later: go round until nothing new turns up.
    bool grew = true;
    while (grew) {
        grew = false;
        for (int row = 0; row < m_items.size(); ++row) {
            const FileItem &it = m_items.at(row);
            if (it.depth == 0 || out.contains(row) || !folders.contains(it.parentPath))
                continue;
            out.insert(row);
            if (it.isDir)
                folders.insert(it.path);
            grew = true;
        }
    }
    return out;
}

void FileListModel::dropRows(QSet<int> rows)
{
    if (rows.isEmpty())
        return;
    QList<int> sorted(rows.cbegin(), rows.cend());
    std::sort(sorted.begin(), sorted.end());
    // From the bottom, a run of neighboring rows at a time.
    int i = int(sorted.size()) - 1;
    while (i >= 0) {
        const int last = sorted.at(i);
        int first = last;
        while (i > 0 && sorted.at(i - 1) == first - 1) {
            --i;
            --first;
        }
        --i;
        for (int r = first; r <= last; ++r) {
            const QString &p = m_items.at(r).path;
            m_thumbIcons.remove(p);
            if (m_items.at(r).isDir)
                m_expanded.remove(p);
        }
        beginRemoveRows({}, first, last);
        m_items.remove(first, last - first + 1);
        m_mimeNames.remove(first, last - first + 1);
        endRemoveRows();
    }
    rebuildIndex();
}

void FileListModel::removeDescendants(const QString &parentPath)
{
    dropRows(descendantRows({parentPath}));
}

void FileListModel::setChildren(const QString &parentPath, const FileItemList &items)
{
    const int prow = rowForPath(parentPath);
    if (prow < 0 || !m_expanded.contains(parentPath))
        return;
    const int depth = m_items.at(prow).depth + 1;
    QHash<QString, int> incoming;
    for (int i = 0; i < items.size(); ++i)
        incoming.insert(items.at(i).path, i);

    // Children that went, and anything under them.
    QSet<int> gone;
    QSet<QString> goneFolders;
    for (int row = 0; row < m_items.size(); ++row) {
        const FileItem &it = m_items.at(row);
        if (it.depth > 0 && it.parentPath == parentPath && !incoming.contains(it.path)) {
            gone.insert(row);
            if (it.isDir)
                goneFolders.insert(it.path);
        }
    }
    gone += descendantRows(goneFolders);
    dropRows(gone);

    // Changed ones in place.
    for (int row = 0; row < m_items.size(); ++row) {
        FileItem &it = m_items[row];
        if (it.depth == 0 || it.parentPath != parentPath)
            continue;
        FileItem fresh = items.at(incoming.value(it.path));
        fresh.parentPath = parentPath;
        fresh.depth = depth;
        if (!it.sameContentAs(fresh)) {
            const bool kindChanged = it.isDir != fresh.isDir || it.name != fresh.name;
            it = fresh;
            m_thumbIcons.remove(fresh.path);
            if (kindChanged)
                m_mimeNames[row].clear();
            Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
        }
    }

    // New ones at the end; the proxy puts them under their folder.
    FileItemList added;
    for (const FileItem &item : items) {
        if (m_rowByPath.contains(item.path))
            continue;
        FileItem child = item;
        child.parentPath = parentPath;
        child.depth = depth;
        added.append(child);
    }
    if (!added.isEmpty()) {
        const int first = m_items.size();
        beginInsertRows({}, first, first + added.size() - 1);
        m_items += added;
        m_mimeNames.resize(m_items.size());
        rebuildIndex();
        endInsertRows();
        if (m_liveSizes)
            requestAllFolderSizes();
    }
}

int FileListModel::fileCount() const
{
    int n = 0;
    for (const FileItem &it : m_items)
        n += it.isDir ? 0 : 1;
    return n;
}

int FileListModel::folderCount() const
{
    return m_items.size() - fileCount();
}

int FileListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

int FileListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString FileListModel::mimeNameFor(int row) const
{
    QString &cached = m_mimeNames[row];
    if (cached.isEmpty()) {
        const FileItem &it = m_items.at(row);
        if (it.isDir)
            cached = QStringLiteral("inode/directory");
        else
            cached = m_mimeDb.mimeTypeForFile(it.path, QMimeDatabase::MatchExtension).name();
        if (!it.isDir && FileKind::extensionOf(it.name) == QLatin1String("crt") && Location::isLocal(it.path)) {
            // .crt is a certificate to the MIME database, but on a Deck it is
            // far more likely a C64 cartridge: those start with this header.
            QFile f(it.path);
            if (f.open(QIODevice::ReadOnly) && f.read(16) == QByteArrayLiteral("C64 CARTRIDGE   "))
                cached = QStringLiteral("application/x-c64-cartridge");
        }
    }
    return cached;
}

QString FileListModel::typeName(int row) const
{
    const QString mime = mimeNameFor(row);
    if (mime == QLatin1String("application/x-c64-cartridge"))
        return QStringLiteral("C64 cartridge image");
    if (mime == QLatin1String("application/octet-stream")) {
        // Unknown to the MIME database: name it after its extension, so a
        // folder of D64s or ADFs does not read "Unknown" all the way down.
        const FileItem &it = m_items.at(row);
        const QString ext = FileKind::extensionOf(it.name).toUpper();
        if (!ext.isEmpty()) {
            return FileKind::categoryFor(it.name, false) == FileKind::Category::DiskImage
                ? QStringLiteral("%1 disk image").arg(ext)
                : QStringLiteral("%1 file").arg(ext);
        }
    }
    auto it = m_typeCache.constFind(mime);
    if (it != m_typeCache.cend())
        return *it;
    QString comment = m_items.at(row).isDir ? QStringLiteral("Folder") : m_mimeDb.mimeTypeForName(mime).comment();
    if (comment.isEmpty())
        comment = mime;
    if (!comment.isEmpty())
        comment[0] = comment[0].toUpper();
    m_typeCache.insert(mime, comment);
    return comment;
}

QIcon FileListModel::iconFor(int row) const
{
    if (m_items.at(row).isDir && m_items.at(row).path.startsWith(QLatin1String("coll:"))) {
        static const QIcon coll = QIcon::fromTheme(QStringLiteral("folder-favorites"), QIcon::fromTheme(QStringLiteral("folder")));
        return coll; // a collection; not cached as "inode/directory"
    }
    const QString mime = mimeNameFor(row);
    auto it = m_iconCache.constFind(mime);
    if (it != m_iconCache.cend())
        return *it;
    QIcon icon;
    const FileItem &item = m_items.at(row);
    if (item.isDir) {
        icon = QIcon::fromTheme(QStringLiteral("folder"));
    } else if (mime == QLatin1String("application/x-c64-cartridge")) {
        icon = QIcon::fromTheme(QStringLiteral("media-flash"), QIcon::fromTheme(QStringLiteral("media-floppy")));
    } else if (mime == QLatin1String("application/octet-stream")
               && FileKind::categoryFor(item.name, false) == FileKind::Category::DiskImage) {
        // Disk images the MIME database does not know (D64, ADF, ...): a disk.
        const QString ext = FileKind::extensionOf(item.name).toLower();
        const bool optical = ext == QLatin1String("iso") || ext == QLatin1String("chd") || ext == QLatin1String("cue")
            || ext == QLatin1String("bin") || ext == QLatin1String("img");
        icon = QIcon::fromTheme(optical ? QStringLiteral("media-optical") : QStringLiteral("media-floppy"));
        if (icon.isNull())
            icon = QIcon::fromTheme(QStringLiteral("drive-removable-media"));
        if (icon.isNull())
            icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
        return icon; // per extension, so not cached under the shared MIME name
    } else {
        const QMimeType type = m_mimeDb.mimeTypeForName(mime);
        icon = QIcon::fromTheme(type.iconName());
        if (icon.isNull() && FileKind::categoryFor(item.name, false) == FileKind::Category::DiskImage)
            icon = QIcon::fromTheme(QStringLiteral("media-floppy")); // ADF, D64 and friends
        if (icon.isNull())
            icon = QIcon::fromTheme(type.genericIconName());
        if (icon.isNull())
            icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
    }
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(m_items.at(row).isDir ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    m_iconCache.insert(mime, icon);
    return icon;
}

namespace {

QString permissionText(QFile::Permissions p, bool isDir)
{
    QString out(10, QLatin1Char('-'));
    out[0] = isDir ? QLatin1Char('d') : QLatin1Char('-');
    const QFile::Permission bits[9] = {QFile::ReadOwner, QFile::WriteOwner, QFile::ExeOwner,
                                       QFile::ReadGroup, QFile::WriteGroup, QFile::ExeGroup,
                                       QFile::ReadOther, QFile::WriteOther, QFile::ExeOther};
    const char letters[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) {
        if (p & bits[i])
            out[i + 1] = QLatin1Char(letters[i]);
    }
    return out;
}

QString dateText(const QDateTime &d)
{
    return d.isValid() ? QLocale().toString(d, QLocale::ShortFormat) : QString();
}

} // namespace

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const FileItem &it = m_items.at(index.row());
    const int col = index.column();

    switch (role) {
    case Qt::DisplayRole:
        switch (col) {
        case NameColumn:
            return it.name;
        case SizeColumn:
            return it.isDir ? folderSizeText(index.row()) : FileOps::formatSize(it.size);
        case TypeColumn:
            return typeName(index.row());
        case ModifiedColumn:
            return dateText(it.modified);
        case CreatedColumn:
            return dateText(it.created);
        case AccessedColumn:
            return dateText(it.accessed);
        case ExtensionColumn:
            return it.isDir ? QString() : FileKind::extensionOf(it.name);
        case PermissionsColumn:
            return permissionText(it.permissions, it.isDir);
        case OwnerColumn:
            return it.owner;
        case GroupColumn:
            return it.group;
        case LocationColumn:
            return it.relPath;
        case RatingColumn:
            return FileTags::stars(it.tags.rating);
        case LabelColumn:
            return it.tags.label;
        case TagsColumn:
            return it.tags.tags.join(QStringLiteral(", "));
        case DescriptionColumn:
            return it.tags.comment.section(QLatin1Char('\n'), 0, 0);
        default:
            if (isMetadataColumn(col))
                return metadataValue(index.row(), col, false);
        }
        break;
    case Qt::EditRole:
        if (col == NameColumn)
            return it.name;
        break;
    case Qt::DecorationRole:
        if (col == NameColumn && m_thumbs) {
            const QVariant thumb = thumbnailFor(index.row());
            if (thumb.isValid())
                return thumb;
        }
        if (col == NameColumn)
            return iconFor(index.row());
        if (col == LabelColumn && !it.tags.label.isEmpty()) {
            const QColor c = FileTags::colorOf(it.tags.label);
            if (c.isValid()) {
                QPixmap dot(12, 12);
                dot.fill(Qt::transparent);
                QPainter p(&dot);
                p.setRenderHint(QPainter::Antialiasing);
                p.setBrush(c);
                p.setPen(c.darker(130));
                p.drawEllipse(QRectF(1, 1, 10, 10));
                return dot;
            }
        }
        break;
    case Qt::ToolTipRole:
        if (col == DescriptionColumn && !it.tags.comment.isEmpty())
            return it.tags.comment;
        if (isMetadataColumn(col) || col == NameColumn) {
            MediaInfo info;
            if (!it.isSymlink && mediaInfo(index.row(), &info) && !info.isEmpty())
                return info.summary();
        }
        if (col == NameColumn && it.isSymlink)
            return QStringLiteral("Link to %1").arg(it.symlinkTarget);
        if (col == SizeColumn && it.isDir && FolderSizes::instance()->has(it.path)) {
            const FolderSizes::Result r = FolderSizes::instance()->result(it.path);
            return QStringLiteral("%1 files, %2 folders%3")
                .arg(r.files)
                .arg(r.folders)
                .arg(r.complete ? QString() : QStringLiteral(" (some folders could not be read)"));
        }
        break;
    case Qt::ForegroundRole: {
        const Style *style = Style::instance();
        QColor c;
        // A color label colors the whole row's text, as in Opus.
        if (!it.tags.label.isEmpty() && (c = FileTags::colorOf(it.tags.label)).isValid()) {
            // labelled: c set
        } else if (const LabelRule *rule = style->labels().match(it.name, it.isDir); rule && rule->color.isValid())
            c = rule->color;
        else if (col == NameColumn || col == ExtensionColumn)
            c = style->theme().typeColor(FileKind::categoryFor(it.name, it.isDir));
        if (it.isHidden) {
            if (!c.isValid())
                c = QApplication::palette().color(QPalette::Text);
            c.setAlphaF(0.5);
        }
        if (c.isValid())
            return c;
        break;
    }
    case Qt::BackgroundRole:
        if (const LabelRule *rule = Style::instance()->labels().match(it.name, it.isDir); rule && rule->background.isValid())
            return rule->background;
        break;
    case Qt::FontRole: {
        const LabelRule *rule = Style::instance()->labels().match(it.name, it.isDir);
        const bool italic = (col == NameColumn && it.isSymlink) || (rule && rule->italic);
        const bool bold = rule && rule->bold;
        if (italic || bold) {
            QFont f = QApplication::font();
            f.setItalic(italic);
            f.setBold(bold);
            return f;
        }
        break;
    }
    case Qt::TextAlignmentRole:
        if (col == SizeColumn || col == TrackColumn || col == YearColumn || col == DurationColumn || col == BitrateColumn)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case PathRole:
        return it.path;
    case IsDirRole:
        return it.isDir;
    case RelPathRole:
        return it.relPath;
    case DepthRole:
        return it.depth;
    case ExpandRole:
        if (!m_expandable)
            return -2; // no triangles in this list at all
        if (!canExpand(index.row()))
            return -1;
        return m_expanded.contains(it.path) ? 1 : 0;
    case FolderBarRole:
        if (m_liveSizes && it.isDir && m_maxFolderBytes > 0 && FolderSizes::instance()->has(it.path))
            return static_cast<double>(FolderSizes::instance()->result(it.path).bytes) / static_cast<double>(m_maxFolderBytes);
        return -1.0;
    case FolderBytesRole:
        if (it.isDir && FolderSizes::instance()->has(it.path))
            return FolderSizes::instance()->result(it.path).bytes;
        return qint64(-1);
    case GroupKeyRole:
        return groupFor(index.row()).first;
    case GroupLabelRole:
        return groupFor(index.row()).second;
    case SortRole:
        switch (col) {
        case SizeColumn:
            if (it.isDir)
                return FolderSizes::instance()->has(it.path) ? FolderSizes::instance()->result(it.path).bytes : qint64(-1);
            return it.size;
        case TypeColumn:
            return typeName(index.row());
        case ModifiedColumn:
            return it.modified;
        case CreatedColumn:
            return it.created;
        case AccessedColumn:
            return it.accessed;
        case ExtensionColumn:
            return FileKind::extensionOf(it.name);
        case PermissionsColumn:
            return static_cast<int>(it.permissions);
        case OwnerColumn:
            return it.owner;
        case GroupColumn:
            return it.group;
        case LocationColumn:
            return it.relPath;
        case RatingColumn:
            return it.tags.rating;
        case LabelColumn:
            return it.tags.label;
        case TagsColumn:
            return it.tags.tags.join(QLatin1Char(','));
        case DescriptionColumn:
            return it.tags.comment;
        default:
            if (isMetadataColumn(col))
                return metadataValue(index.row(), col, true);
            return it.name;
        }
    }
    return {};
}

bool FileListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.column() != NameColumn)
        return false;
    const QString newName = value.toString().trimmed();
    const FileItem &it = m_items.at(index.row());
    if (newName.isEmpty() || newName == it.name)
        return false;
    Q_EMIT renameRequested(it.path, newName);
    // The real rename runs as a command; the refresh shows the result.
    return false;
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return columnName(section);
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (index.column() == NameColumn)
        f |= Qt::ItemIsEditable;
    if (m_items.at(index.row()).isDir)
        f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList FileListModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

QMimeData *FileListModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;
    QSet<int> rows;
    for (const QModelIndex &i : indexes) {
        if (i.isValid() && !rows.contains(i.row())) {
            rows.insert(i.row());
            urls << QUrl::fromLocalFile(m_items.at(i.row()).path);
        }
    }
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

Qt::DropActions FileListModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions FileListModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

bool FileListModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const
{
    if (!data || !data->hasUrls())
        return false;
    if (parent.isValid() && !m_items.at(parent.row()).isDir)
        return false;
    return true;
}

bool FileListModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int, int, const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, -1, -1, parent))
        return false;
    const QString target = parent.isValid() ? m_items.at(parent.row()).path : m_dir;
    Q_EMIT dropRequested(data->urls(), target, action);
    return true;
}
