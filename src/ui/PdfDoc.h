#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>

#include <memory>

// A PDF to show: its pages as pictures. Uses Poppler (as Okular does) or,
// without it, Qt PDF; neither: available() is false and nothing loads.
class PdfDoc
{
public:
    PdfDoc();
    ~PdfDoc();
    static bool available();
    bool load(const QString &path);
    bool isLoaded() const;
    int pageCount() const;
    QSizeF pagePoints(int page) const; // in points (1/72 inch)
    QImage render(int page, const QSize &pixels) const; // on white
    QString title() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
