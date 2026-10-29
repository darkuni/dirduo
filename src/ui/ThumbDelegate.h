#pragma once

#include "ui/RenameDelegate.h"

// Thumbnails mode, drawn the way Preferences > Thumbnails says: each picture
// fitted or filling its square, in a frame (none, a thin border, a shadow, a
// photo print or a 35mm slide), with film edges on videos, the rating, the
// type's icon and a bar for its size next to the biggest; under it the name
// on up to three lines, and the size and date when asked.
class ThumbDelegate : public RenameDelegate
{
    Q_OBJECT
public:
    using RenameDelegate::RenameDelegate;

    // The square each thumbnail gets, in logical pixels.
    void setPixels(int px) { m_px = px; }
    int pixels() const { return m_px; }

    // The cell (QListView grid size) for these settings and font.
    static QSize cellSize(int px, const QFont &font);

    // Where the picture goes in a cell, and where the label goes.
    QRect imageRect(const QRect &cell) const;
    QRect labelRect(const QRect &cell) const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // The label's lines for an item (tests).
    static QStringList detailLines(const QModelIndex &index);

private:
    void drawPicture(QPainter *p, const QRect &box, const QImage &img, const QModelIndex &index, bool video) const;
    void drawIcon(QPainter *p, const QRect &box, const QModelIndex &index, const QImage &collage) const;
    int m_px = 128;
};
