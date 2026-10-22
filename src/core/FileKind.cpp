#include "core/FileKind.h"

#include <QHash>
#include <QStringList>

namespace FileKind {

namespace {

const QHash<QString, Category> &table()
{
    static const QHash<QString, Category> t = [] {
        QHash<QString, Category> h;
        auto add = [&h](Category c, const QStringList &exts) {
            for (const QString &e : exts)
                h.insert(e, c);
        };
        add(Category::Image, {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"),
                              QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("tif"), QStringLiteral("tiff"),
                              QStringLiteral("svg"), QStringLiteral("heic"), QStringLiteral("avif"), QStringLiteral("raw"),
                              QStringLiteral("cr2"), QStringLiteral("nef"), QStringLiteral("dng"), QStringLiteral("ico"),
                              QStringLiteral("psd"), QStringLiteral("xcf"), QStringLiteral("iff"), QStringLiteral("ilbm")});
        add(Category::Audio, {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"),
                              QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"),
                              QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("ape"), QStringLiteral("mod"),
                              QStringLiteral("xm"), QStringLiteral("s3m"), QStringLiteral("it"), QStringLiteral("sid"),
                              QStringLiteral("mid"), QStringLiteral("midi"), QStringLiteral("vgm"), QStringLiteral("spc")});
        add(Category::Video, {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"),
                              QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("wmv"), QStringLiteral("mpg"),
                              QStringLiteral("mpeg"), QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("flv"),
                              QStringLiteral("vob"), QStringLiteral("3gp")});
        add(Category::Archive, {QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("tar"),
                                QStringLiteral("gz"), QStringLiteral("tgz"), QStringLiteral("bz2"), QStringLiteral("xz"),
                                QStringLiteral("zst"), QStringLiteral("lha"), QStringLiteral("lzh"), QStringLiteral("lzx"),
                                QStringLiteral("cab"), QStringLiteral("deb"), QStringLiteral("rpm")});
        add(Category::DiskImage, {QStringLiteral("iso"), QStringLiteral("img"), QStringLiteral("bin"), QStringLiteral("cue"),
                                  QStringLiteral("chd"), QStringLiteral("cso"), QStringLiteral("rvz"), QStringLiteral("wbfs"),
                                  QStringLiteral("d64"), QStringLiteral("d71"), QStringLiteral("d81"), QStringLiteral("t64"),
                                  QStringLiteral("tap"), QStringLiteral("crt"), QStringLiteral("g64"), QStringLiteral("adf"),
                                  QStringLiteral("hdf"), QStringLiteral("dms"), QStringLiteral("ipf"), QStringLiteral("dsk"),
                                  QStringLiteral("mdf"), QStringLiteral("nrg"), QStringLiteral("gdi"), QStringLiteral("vhd"),
                                  QStringLiteral("qcow2"), QStringLiteral("appimage")});
        add(Category::Document, {QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"), QStringLiteral("odt"),
                                 QStringLiteral("rtf"), QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("nfo"),
                                 QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ods"), QStringLiteral("csv"),
                                 QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("odp"), QStringLiteral("epub"),
                                 QStringLiteral("cbz"), QStringLiteral("cbr"), QStringLiteral("log")});
        add(Category::Code, {QStringLiteral("c"), QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("hpp"),
                             QStringLiteral("py"), QStringLiteral("js"), QStringLiteral("ts"), QStringLiteral("json"),
                             QStringLiteral("xml"), QStringLiteral("html"), QStringLiteral("css"), QStringLiteral("sh"),
                             QStringLiteral("rs"), QStringLiteral("go"), QStringLiteral("java"), QStringLiteral("ini"),
                             QStringLiteral("yml"), QStringLiteral("yaml"), QStringLiteral("toml"), QStringLiteral("cmake"),
                             QStringLiteral("asm"), QStringLiteral("s"), QStringLiteral("bb"), QStringLiteral("bas"),
                             QStringLiteral("lua"), QStringLiteral("php"), QStringLiteral("qml")});
        add(Category::Executable, {QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("bat"), QStringLiteral("cmd"),
                                   QStringLiteral("desktop"), QStringLiteral("run"), QStringLiteral("flatpakref"),
                                   QStringLiteral("prg"), QStringLiteral("so")});
        // "ts" is far more often a video stream on a Deck than TypeScript.
        h.insert(QStringLiteral("ts"), Category::Video);
        return h;
    }();
    return t;
}

} // namespace

QString extensionOf(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1)
        return {};
    return name.mid(dot + 1).toLower();
}

Category categoryFor(const QString &name, bool isDir)
{
    if (isDir)
        return Category::Folder;
    const QString ext = extensionOf(name);
    if (ext.isEmpty())
        return Category::Other;
    return table().value(ext, Category::Other);
}

QString categoryName(Category c)
{
    switch (c) {
    case Category::Folder:
        return QStringLiteral("Folders");
    case Category::Image:
        return QStringLiteral("Images");
    case Category::Audio:
        return QStringLiteral("Audio");
    case Category::Video:
        return QStringLiteral("Video");
    case Category::Archive:
        return QStringLiteral("Archives");
    case Category::DiskImage:
        return QStringLiteral("Disk images");
    case Category::Document:
        return QStringLiteral("Documents");
    case Category::Code:
        return QStringLiteral("Code and text");
    case Category::Executable:
        return QStringLiteral("Programs");
    case Category::Other:
        return QStringLiteral("Other files");
    }
    return {};
}

QString categoryKey(Category c)
{
    switch (c) {
    case Category::Folder:
        return QStringLiteral("folder");
    case Category::Image:
        return QStringLiteral("image");
    case Category::Audio:
        return QStringLiteral("audio");
    case Category::Video:
        return QStringLiteral("video");
    case Category::Archive:
        return QStringLiteral("archive");
    case Category::DiskImage:
        return QStringLiteral("diskimage");
    case Category::Document:
        return QStringLiteral("document");
    case Category::Code:
        return QStringLiteral("code");
    case Category::Executable:
        return QStringLiteral("program");
    case Category::Other:
        return QStringLiteral("other");
    }
    return {};
}

bool categoryFromKey(const QString &key, Category &out)
{
    for (int i = 0; i <= static_cast<int>(Category::Other); ++i) {
        const auto c = static_cast<Category>(i);
        if (categoryKey(c) == key.toLower()) {
            out = c;
            return true;
        }
    }
    return false;
}

} // namespace FileKind
