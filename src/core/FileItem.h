#pragma once

#include <QDateTime>
#include <QFile>
#include <QMetaType>
#include <QString>
#include <QVector>

class QFileInfo;

// One entry in a folder listing. Plain data so it can cross threads.
struct FileItem
{
    QString name;
    QString path;
    QString symlinkTarget;
    QString owner;
    QString group;
    QString relPath; // flat view: folder relative to the flat root ("" at the top)
    QString parentPath; // inside an expanded folder: that folder ("" at the top)
    int depth = 0;      // how many expanded folders deep
    QDateTime modified;
    QDateTime created;
    QDateTime accessed;
    qint64 size = 0;
    QFile::Permissions permissions;
    bool isDir = false;
    bool isSymlink = false;
    bool isHidden = false;
    bool isReadable = true;

    static FileItem fromFileInfo(const QFileInfo &info);

    // True when anything shown in a column differs.
    bool sameContentAs(const FileItem &o) const
    {
        return name == o.name && size == o.size && modified == o.modified && isDir == o.isDir && relPath == o.relPath
            && isSymlink == o.isSymlink && permissions == o.permissions && symlinkTarget == o.symlinkTarget;
    }
};

using FileItemList = QVector<FileItem>;

Q_DECLARE_METATYPE(FileItem)
Q_DECLARE_METATYPE(FileItemList)
