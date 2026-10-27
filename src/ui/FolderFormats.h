#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// How a folder looks: columns and their order and widths, the sort, the
// grouping and the view mode. Saved for a folder (optionally with all its
// subfolders) or as the default for folders without their own, in
// ~/.config/dirduo/formats.ini.
struct FolderFormat
{
    QString path;          // empty for the default format
    bool subfolders = false;
    QByteArray header;     // QHeaderView state: columns, widths, sort
    QString groupBy;       // FileListModel::groupByName
    QString view;          // FolderTab::viewModeName
};

class FolderFormats
{
public:
    static FolderFormats &instance();

    // The format for path: its own, else the nearest parent saved "with
    // subfolders". False when neither exists.
    bool find(const QString &path, FolderFormat &out) const;
    bool defaultFormat(FolderFormat &out) const;

    void set(const FolderFormat &format);
    bool remove(const QString &path); // the folder's own format
    void setDefault(const FolderFormat &format);
    void clearDefault();

    // Folders without a format switch to icons when they are mostly pictures.
    bool autoSwitch() const { return m_autoSwitch; }
    void setAutoSwitch(bool on);

    QList<FolderFormat> all() const { return m_formats; }

    // Tests point this somewhere else.
    void setFile(const QString &file);
    void reload();

private:
    FolderFormats();
    void save() const;
    QString file() const;

    QString m_file;
    QList<FolderFormat> m_formats;
    bool m_hasDefault = false;
    FolderFormat m_default;
    bool m_autoSwitch = true;
};
