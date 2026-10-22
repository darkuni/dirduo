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
    QDateTime modified;
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
        return name == o.name && size == o.size && modified == o.modified && isDir == o.isDir
            && isSymlink == o.isSymlink && permissions == o.permissions && symlinkTarget == o.symlinkTarget;
    }
};

using FileItemList = QVector<FileItem>;

Q_DECLARE_METATYPE(FileItem)
Q_DECLARE_METATYPE(FileItemList)
