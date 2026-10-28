#include "ui/PdfDoc.h"

#include <QPainter>

#if defined(DIRDUO_HAVE_POPPLER)
#include <poppler-qt6.h>
#elif defined(DIRDUO_HAVE_QTPDF)
#include <QPdfDocument>
#endif

struct PdfDoc::Impl
{
#if defined(DIRDUO_HAVE_POPPLER)
    std::unique_ptr<Poppler::Document> doc;
#elif defined(DIRDUO_HAVE_QTPDF)
    QPdfDocument doc;
    bool ok = false;
#endif
};

PdfDoc::PdfDoc()
    : d(std::make_unique<Impl>())
{
}

PdfDoc::~PdfDoc() = default;

bool PdfDoc::available()
{
#if defined(DIRDUO_HAVE_POPPLER) || defined(DIRDUO_HAVE_QTPDF)
    return true;
#else
    return false;
#endif
}

bool PdfDoc::load(const QString &path)
{
#if defined(DIRDUO_HAVE_POPPLER)
    d->doc = Poppler::Document::load(path);
    if (!d->doc || d->doc->isLocked()) {
        d->doc.reset();
        return false;
    }
    d->doc->setRenderHint(Poppler::Document::Antialiasing, true);
    d->doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
    d->doc->setPaperColor(Qt::white);
    return d->doc->numPages() > 0;
#elif defined(DIRDUO_HAVE_QTPDF)
    d->doc.load(path);
    d->ok = d->doc.status() == QPdfDocument::Status::Ready && d->doc.pageCount() > 0;
    return d->ok;
#else
    Q_UNUSED(path)
    return false;
#endif
}

bool PdfDoc::isLoaded() const
{
#if defined(DIRDUO_HAVE_POPPLER)
    return bool(d->doc);
#elif defined(DIRDUO_HAVE_QTPDF)
    return d->ok;
#else
    return false;
#endif
}

int PdfDoc::pageCount() const
{
#if defined(DIRDUO_HAVE_POPPLER)
    return d->doc ? d->doc->numPages() : 0;
#elif defined(DIRDUO_HAVE_QTPDF)
    return d->ok ? d->doc.pageCount() : 0;
#else
    return 0;
#endif
}

QSizeF PdfDoc::pagePoints(int page) const
{
#if defined(DIRDUO_HAVE_POPPLER)
    if (!d->doc)
        return {};
    std::unique_ptr<Poppler::Page> p = d->doc->page(page);
    return p ? p->pageSizeF() : QSizeF();
#elif defined(DIRDUO_HAVE_QTPDF)
    return d->ok ? d->doc.pagePointSize(page) : QSizeF();
#else
    Q_UNUSED(page)
    return {};
#endif
}

QImage PdfDoc::render(int page, const QSize &pixels) const
{
    QImage img;
#if defined(DIRDUO_HAVE_POPPLER)
    if (!d->doc)
        return {};
    std::unique_ptr<Poppler::Page> p = d->doc->page(page);
    if (!p)
        return {};
    const QSizeF pt = p->pageSizeF();
    if (pt.width() <= 0 || pt.height() <= 0)
        return {};
    // Resolution that makes the page this many pixels across.
    const double xres = 72.0 * pixels.width() / pt.width();
    const double yres = 72.0 * pixels.height() / pt.height();
    img = p->renderToImage(xres, yres);
#elif defined(DIRDUO_HAVE_QTPDF)
    if (!d->ok)
        return {};
    img = d->doc.render(page, pixels);
#else
    Q_UNUSED(page)
    Q_UNUSED(pixels)
#endif
    if (img.isNull() || !img.hasAlphaChannel())
        return img;
    // On white paper: pages are see-through where nothing is drawn.
    QImage out(img.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.drawImage(0, 0, img);
    return out;
}

QString PdfDoc::title() const
{
#if defined(DIRDUO_HAVE_POPPLER)
    return d->doc ? d->doc->info(QStringLiteral("Title")).trimmed() : QString();
#elif defined(DIRDUO_HAVE_QTPDF)
    return d->ok ? d->doc.metaData(QPdfDocument::MetaDataField::Title).toString().trimmed() : QString();
#else
    return {};
#endif
}
