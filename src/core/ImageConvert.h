#pragma once

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>

// Converts pictures between formats, resizing, rotating and flipping them on
// the way. Reads whatever Qt's image plugins read on the machine (the Flatpak
// runtime adds WebP, TIFF, AVIF, HEIF and more); always writes PNG, JPEG and
// BMP, plus whatever else the plugins can write.
struct ImageOptions
{
    QString format;   // "png", "jpg"... empty: keep each picture's own format
    int quality = 90; // 1-100, for JPEG, WebP and AVIF

    enum class Resize { None, Percent, Fit, Width, Height, Exact };
    Resize resize = Resize::None;
    int percent = 50;
    int width = 1920;
    int height = 1080;
    bool noEnlarge = true; // never make a picture bigger

    int rotate = 0; // clockwise degrees: 0, 90, 180, 270
    bool flipH = false;
    bool flipV = false;

    QString destFolder; // empty: next to each picture
    enum class WhenExists { Rename, Replace, Skip };
    WhenExists whenExists = WhenExists::Rename;

    static QString resizeName(Resize r);
    static bool resizeFromName(const QString &name, Resize &out);
    static QString whenExistsName(WhenExists w);
    static bool whenExistsFromName(const QString &name, WhenExists &out);
};

struct ImagePlan
{
    QString source;
    QString target;   // empty when skipped
    QSize sourceSize; // invalid when the picture could not be read
    QSize targetSize;
    QString problem;  // why it will not be converted
};

struct ImageResult
{
    QString source;
    QString target; // what was written; empty when nothing was
    QString error;  // empty when it worked or was skipped
    bool skipped = false;
};

struct ImageProgress
{
    std::atomic<int> done{0};
    std::atomic<bool> cancel{false};
};

namespace ImageConvert {

// Lower-case format names: what can be read, what can be written.
QStringList readableFormats();
QStringList writableFormats();
// Is this file a picture that can be read (by extension, then by contents)?
bool canRead(const QString &path);
QString displayName(const QString &format); // "JPEG", "PNG"...
bool hasQuality(const QString &format);
bool hasAlpha(const QString &format);
// "jpg" for jpeg and so on: the extension to give a converted file.
QString extensionFor(const QString &format);
// Normalises a format name the user typed ("JPEG" -> "jpg"); empty if unknown
// or not writable.
QString normaliseFormat(const QString &name);

QSize targetSize(const QSize &source, const ImageOptions &o);
// Where a picture will go, before collisions are dealt with.
QString targetPathFor(const QString &source, const ImageOptions &o);

// What will happen to each picture, without writing anything. Reads only the
// picture headers. Two pictures that would land on the same name get
// different names here when renaming.
QList<ImagePlan> plan(const QStringList &sources, const ImageOptions &o);

// Converts one picture to plan.target.
ImageResult convert(const ImagePlan &plan, const ImageOptions &o);
// Converts them all; safe to run on another thread.
QList<ImageResult> run(const QList<ImagePlan> &plans, const ImageOptions &o, ImageProgress *progress = nullptr);

} // namespace ImageConvert
