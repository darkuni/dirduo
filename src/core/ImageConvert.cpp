#include "core/ImageConvert.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QTransform>

QString ImageOptions::resizeName(Resize r)
{
    switch (r) {
    case Resize::None: return QStringLiteral("none");
    case Resize::Percent: return QStringLiteral("percent");
    case Resize::Fit: return QStringLiteral("fit");
    case Resize::Width: return QStringLiteral("width");
    case Resize::Height: return QStringLiteral("height");
    case Resize::Exact: return QStringLiteral("exact");
    }
    return {};
}

bool ImageOptions::resizeFromName(const QString &name, Resize &out)
{
    for (Resize r : {Resize::None, Resize::Percent, Resize::Fit, Resize::Width, Resize::Height, Resize::Exact}) {
        if (name.compare(resizeName(r), Qt::CaseInsensitive) == 0) {
            out = r;
            return true;
        }
    }
    return false;
}

QString ImageOptions::whenExistsName(WhenExists w)
{
    switch (w) {
    case WhenExists::Rename: return QStringLiteral("rename");
    case WhenExists::Replace: return QStringLiteral("replace");
    case WhenExists::Skip: return QStringLiteral("skip");
    }
    return {};
}

bool ImageOptions::whenExistsFromName(const QString &name, WhenExists &out)
{
    for (WhenExists w : {WhenExists::Rename, WhenExists::Replace, WhenExists::Skip}) {
        if (name.compare(whenExistsName(w), Qt::CaseInsensitive) == 0) {
            out = w;
            return true;
        }
    }
    return false;
}

namespace ImageConvert {

namespace {

QStringList toNames(const QList<QByteArray> &list)
{
    QStringList out;
    for (const QByteArray &b : list) {
        const QString s = QString::fromLatin1(b).toLower();
        if (!out.contains(s))
            out << s;
    }
    return out;
}

// Same name, different case or spelling: "jpeg" and "jpg" are one format.
QString canonical(const QString &format)
{
    const QString f = format.toLower();
    if (f == QLatin1String("jpeg") || f == QLatin1String("jpe"))
        return QStringLiteral("jpg");
    if (f == QLatin1String("tif"))
        return QStringLiteral("tiff");
    return f;
}

// "photo.jpg" -> "photo (2).jpg", skipping names already taken.
QString uniqueName(const QString &path, const QSet<QString> &taken)
{
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    const QString base = fi.completeBaseName();
    const QString ext = fi.suffix();
    for (int n = 2;; ++n) {
        QString name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!ext.isEmpty())
            name += QLatin1Char('.') + ext;
        const QString candidate = QDir(dir).filePath(name);
        if (!taken.contains(candidate) && !QFileInfo::exists(candidate))
            return candidate;
    }
}

} // namespace

QStringList readableFormats()
{
    static const QStringList formats = toNames(QImageReader::supportedImageFormats());
    return formats;
}

QStringList writableFormats()
{
    // Common ones first, in the order people want them; then the rest.
    static const QStringList formats = []() {
        QStringList all;
        for (const QString &f : toNames(QImageWriter::supportedImageFormats())) {
            const QString c = canonical(f);
            if (!all.contains(c))
                all << c;
        }
        const QStringList preferred = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp"),
                                       QStringLiteral("avif"), QStringLiteral("jxl"), QStringLiteral("tiff"),
                                       QStringLiteral("bmp"), QStringLiteral("ico")};
        // Formats nobody converts photos into on purpose.
        const QStringList hidden = {QStringLiteral("cur"), QStringLiteral("icns"), QStringLiteral("pbm"),
                                    QStringLiteral("pgm"), QStringLiteral("ppm"), QStringLiteral("xbm"),
                                    QStringLiteral("xpm"), QStringLiteral("wbmp")};
        QStringList out;
        for (const QString &f : preferred) {
            if (all.contains(f))
                out << f;
        }
        for (const QString &f : std::as_const(all)) {
            if (!out.contains(f) && !hidden.contains(f))
                out << f;
        }
        return out;
    }();
    return formats;
}

bool canRead(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile())
        return false;
    const QString ext = fi.suffix().toLower();
    if (!ext.isEmpty() && readableFormats().contains(ext))
        return true;
    QImageReader reader(path);
    return !reader.format().isEmpty();
}

QString displayName(const QString &format)
{
    const QString f = canonical(format);
    if (f == QLatin1String("jpg"))
        return QStringLiteral("JPEG");
    if (f == QLatin1String("webp"))
        return QStringLiteral("WebP");
    if (f == QLatin1String("jxl"))
        return QStringLiteral("JPEG XL");
    return f.toUpper();
}

bool hasQuality(const QString &format)
{
    const QString f = canonical(format);
    return f == QLatin1String("jpg") || f == QLatin1String("webp") || f == QLatin1String("avif")
        || f == QLatin1String("jxl") || f == QLatin1String("heif") || f == QLatin1String("heic");
}

bool hasAlpha(const QString &format)
{
    const QString f = canonical(format);
    return !(f == QLatin1String("jpg") || f == QLatin1String("bmp") || f == QLatin1String("ppm")
             || f == QLatin1String("pgm") || f == QLatin1String("pbm"));
}

QString extensionFor(const QString &format)
{
    return canonical(format);
}

QString normaliseFormat(const QString &name)
{
    QString f = canonical(name.trimmed());
    if (f.startsWith(QLatin1Char('.')))
        f.remove(0, 1);
    return writableFormats().contains(f) ? f : QString();
}

QSize targetSize(const QSize &source, const ImageOptions &o)
{
    if (!source.isValid() || source.isEmpty())
        return source;
    // Sizes are of the picture as it ends up: turned first, then resized.
    QSize src = source;
    if (o.rotate == 90 || o.rotate == 270)
        src.transpose();
    QSize out = src;
    switch (o.resize) {
    case ImageOptions::Resize::None:
        break;
    case ImageOptions::Resize::Percent:
        out = QSize(qMax(1, qRound(src.width() * o.percent / 100.0)), qMax(1, qRound(src.height() * o.percent / 100.0)));
        break;
    case ImageOptions::Resize::Fit:
        out = src.scaled(QSize(qMax(1, o.width), qMax(1, o.height)), Qt::KeepAspectRatio);
        break;
    case ImageOptions::Resize::Width:
        out = QSize(qMax(1, o.width), qMax(1, qRound(double(src.height()) * o.width / src.width())));
        break;
    case ImageOptions::Resize::Height:
        out = QSize(qMax(1, qRound(double(src.width()) * o.height / src.height())), qMax(1, o.height));
        break;
    case ImageOptions::Resize::Exact:
        out = QSize(qMax(1, o.width), qMax(1, o.height));
        break;
    }
    if (o.noEnlarge && (out.width() > src.width() || out.height() > src.height())) {
        if (o.resize == ImageOptions::Resize::Exact)
            out = out.boundedTo(src);
        else
            out = src;
    }
    return out;
}

QString targetPathFor(const QString &source, const ImageOptions &o)
{
    const QFileInfo fi(source);
    const QString dir = o.destFolder.isEmpty() ? fi.absolutePath() : o.destFolder;
    QString ext = o.format.isEmpty() ? fi.suffix() : extensionFor(o.format);
    QString name = fi.completeBaseName();
    if (!ext.isEmpty())
        name += QLatin1Char('.') + ext;
    return QDir(dir).filePath(name);
}

QList<ImagePlan> plan(const QStringList &sources, const ImageOptions &o)
{
    QList<ImagePlan> out;
    QSet<QString> taken; // targets already handed out in this batch
    for (const QString &src : sources) {
        ImagePlan p;
        p.source = src;
        QImageReader reader(src);
        reader.setAutoTransform(true);
        if (!QFileInfo(src).isFile()) {
            p.problem = QStringLiteral("Not a file");
        } else if (!reader.canRead()) {
            p.problem = QStringLiteral("Not a picture this can read");
        } else {
            QSize size = reader.size();
            // size() is before the camera's rotation is applied.
            const QImageIOHandler::Transformations t = reader.transformation();
            if (size.isValid() && (t & QImageIOHandler::TransformationRotate90))
                size.transpose();
            p.sourceSize = size;
            p.targetSize = targetSize(size, o);
            const QString fmt = o.format.isEmpty() ? canonical(QString::fromLatin1(reader.format())) : o.format;
            if (!writableFormats().contains(canonical(fmt))) {
                p.problem = QStringLiteral("Cannot write %1; pick a format").arg(displayName(fmt));
            }
        }
        if (p.problem.isEmpty()) {
            QString target = targetPathFor(src, o);
            const bool sameFile = QFileInfo(target).absoluteFilePath() == QFileInfo(src).absoluteFilePath();
            const bool clash = taken.contains(target) || (QFileInfo::exists(target) && !sameFile);
            if (sameFile && o.whenExists != ImageOptions::WhenExists::Replace) {
                // Converting a picture onto itself only happens when asked to replace.
                if (o.whenExists == ImageOptions::WhenExists::Skip)
                    p.problem = QStringLiteral("Would replace itself");
                else
                    target = uniqueName(target, taken);
            } else if (clash) {
                if (o.whenExists == ImageOptions::WhenExists::Rename)
                    target = uniqueName(target, taken);
                else if (o.whenExists == ImageOptions::WhenExists::Skip)
                    p.problem = QStringLiteral("%1 already exists").arg(QFileInfo(target).fileName());
                else if (taken.contains(target))
                    target = uniqueName(target, taken); // two of the batch never overwrite each other
            }
            if (p.problem.isEmpty()) {
                p.target = target;
                taken.insert(target);
            }
        }
        out << p;
    }
    return out;
}

ImageResult convert(const ImagePlan &plan, const ImageOptions &o)
{
    ImageResult r;
    r.source = plan.source;
    if (plan.target.isEmpty()) {
        r.skipped = true;
        r.error = plan.problem;
        return r;
    }
    QImageReader reader(plan.source);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        r.error = reader.errorString();
        return r;
    }

    if (o.rotate != 0 || o.flipH || o.flipV) {
        QTransform t;
        t.rotate(o.rotate);
        img = img.transformed(t, Qt::SmoothTransformation);
        if (o.flipH || o.flipV) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            Qt::Orientations dirs;
            if (o.flipH)
                dirs |= Qt::Horizontal;
            if (o.flipV)
                dirs |= Qt::Vertical;
            img = img.flipped(dirs);
#else
            img = img.mirrored(o.flipH, o.flipV);
#endif
        }
    }

    const QSize size = plan.targetSize.isValid() ? plan.targetSize : img.size();
    if (size != img.size())
        img = img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QString fmt = o.format.isEmpty() ? canonical(QString::fromLatin1(reader.format())) : o.format;
    if (img.hasAlphaChannel() && !hasAlpha(fmt)) {
        // JPEG and BMP have no transparency: put it on white, not black.
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.setDevicePixelRatio(img.devicePixelRatio());
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, img);
        p.end();
        img = flat;
    }

    const QString dir = QFileInfo(plan.target).absolutePath();
    if (!QDir().mkpath(dir)) {
        r.error = QStringLiteral("Cannot make the folder %1").arg(dir);
        return r;
    }
    // QSaveFile: a failed write never leaves half a picture, and replacing the
    // source itself works because it is only swapped in at the end.
    QSaveFile file(plan.target);
    if (!file.open(QIODevice::WriteOnly)) {
        r.error = file.errorString();
        return r;
    }
    QByteArray writerFormat = fmt.toLatin1();
    if (writerFormat == "jpg")
        writerFormat = "jpeg";
    QImageWriter writer(&file, writerFormat);
    if (hasQuality(fmt))
        writer.setQuality(qBound(1, o.quality, 100));
    if (!writer.write(img)) {
        file.cancelWriting();
        r.error = writer.errorString();
        return r;
    }
    if (!file.commit()) {
        r.error = file.errorString();
        return r;
    }
    r.target = plan.target;
    return r;
}

QList<ImageResult> run(const QList<ImagePlan> &plans, const ImageOptions &o, ImageProgress *progress)
{
    QList<ImageResult> out;
    for (const ImagePlan &p : plans) {
        if (progress && progress->cancel) {
            ImageResult r;
            r.source = p.source;
            r.skipped = true;
            r.error = QStringLiteral("Cancelled");
            out << r;
            continue;
        }
        out << convert(p, o);
        if (progress)
            ++progress->done;
    }
    return out;
}

} // namespace ImageConvert
