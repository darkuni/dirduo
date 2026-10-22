#pragma once

#include <QString>

// Broad file categories, decided by extension so it costs nothing while
// listing. Drives type colors and the "Type" grouping.
namespace FileKind {

enum class Category {
    Folder,
    Image,
    Audio,
    Video,
    Archive,
    DiskImage, // ISO, CHD, D64, ADF and friends
    Document,
    Code,
    Executable,
    Other,
};

Category categoryFor(const QString &name, bool isDir);
QString categoryName(Category c);       // "Images", "Disk images"...
QString categoryKey(Category c);        // "image", "diskimage"... for theme files
bool categoryFromKey(const QString &key, Category &out);
QString extensionOf(const QString &name); // lower case, without the dot; "" for none

} // namespace FileKind
