#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

// A location is either a plain local path ("/home/deck/Music") or a URL for
// anything else ("sftp://deck@nas/media", "ftp://host/pub", "smb://box/share",
// "zip:/home/deck/roms.zip/c64"). Local paths stay plain strings everywhere, so
// the local code paths never pay for URL handling.
namespace Location {

// True for anything with a scheme other than file:.
bool isRemote(const QString &loc);
bool isLocal(const QString &loc);

// True for KIO's archive protocols (zip:, tar:, ar:, sevenz: ...), whose path
// starts with the local archive file.
bool isArchive(const QString &loc);
QStringList archiveSchemes();

QUrl toUrl(const QString &loc);
QString fromUrl(const QUrl &url);
QList<QUrl> toUrls(const QStringList &locs);

// Normalised: "/a/./b/" -> "/a/b"; "sftp://h/a//b/" -> "sftp://h/a/b"; a
// server root keeps its slash ("sftp://h/").
QString clean(const QString &loc);

// One level up. A server root is its own parent. The root of an archive
// goes back out to the local folder holding the archive file.
QString parent(const QString &loc);

QString fileName(const QString &loc);
QString join(const QString &dir, const QString &name);
bool isRoot(const QString &loc);
bool isSameOrInside(const QString &child, const QString &parent);

// Short name for tabs and titles: folder name, or the host for a server root.
QString title(const QString &loc);

// Safe to show: the password, if any, is left out.
QString display(const QString &loc);

// "sftp://deck@nas:22" for a remote location, empty for a local one. Jobs
// writing to the same server queue behind each other.
QString serverKey(const QString &loc);

// For archive locations: the local path of the archive file itself.
QString archiveFile(const QString &loc);

} // namespace Location
