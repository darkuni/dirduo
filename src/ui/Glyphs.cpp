#include "ui/Glyphs.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace Glyphs {

QPixmap pixmap(Glyph g, const QColor &color, int size, qreal dpr)
{
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // Drawn on a 24-unit grid.
    p.scale(size / 24.0, size / 24.0);
    QPen pen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto arrow = [&](QPointF from, QPointF to) {
        p.drawLine(from, to);
        const QLineF l(to, from);
        QLineF a = l;
        a.setLength(7);
        a.setAngle(l.angle() + 40);
        QLineF b = l;
        b.setLength(7);
        b.setAngle(l.angle() - 40);
        p.drawLine(a);
        p.drawLine(b);
    };
    switch (g) {
    case Glyph::Back:
        arrow({20, 12}, {4.5, 12});
        break;
    case Glyph::Forward:
        arrow({4, 12}, {19.5, 12});
        break;
    case Glyph::Up:
        arrow({12, 20}, {12, 4.5});
        break;
    case Glyph::Refresh: {
        QPainterPath path;
        path.arcMoveTo(QRectF(4.5, 4.5, 15, 15), 60);
        path.arcTo(QRectF(4.5, 4.5, 15, 15), 60, 300);
        p.drawPath(path);
        // Arrow head at the start of the arc (top right).
        const QPointF tip(12 + 7.5 * qCos(qDegreesToRadians(60.0)), 12 - 7.5 * qSin(qDegreesToRadians(60.0)));
        QPainterPath head;
        head.moveTo(tip + QPointF(-5.5, -1.5));
        head.lineTo(tip);
        head.lineTo(tip + QPointF(-0.5, 5.5));
        p.drawPath(head);
        break;
    }
    case Glyph::Home: {
        QPainterPath roof;
        roof.moveTo(3.5, 12);
        roof.lineTo(12, 4);
        roof.lineTo(20.5, 12);
        p.drawPath(roof);
        QPainterPath house;
        house.moveTo(6.5, 10);
        house.lineTo(6.5, 20);
        house.lineTo(17.5, 20);
        house.lineTo(17.5, 10);
        p.drawPath(house);
        p.drawLine(QPointF(10.5, 20), QPointF(10.5, 15));
        p.drawLine(QPointF(10.5, 15), QPointF(13.5, 15));
        p.drawLine(QPointF(13.5, 15), QPointF(13.5, 20));
        break;
    }
    case Glyph::Star: {
        // Filled gold star, as in Opus.
        QPainterPath star;
        for (int k = 0; k < 10; ++k) {
            const double r = k % 2 == 0 ? 10.0 : 4.3;
            const double a = qDegreesToRadians(-90.0 + k * 36.0);
            const QPointF pt(12 + r * qCos(a), 12.6 + r * qSin(a));
            if (k == 0)
                star.moveTo(pt);
            else
                star.lineTo(pt);
        }
        star.closeSubpath();
        p.setPen(QPen(QColor(0xb8, 0x86, 0x0b), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(0xf5, 0xc5, 0x42));
        p.drawPath(star);
        break;
    }
    case Glyph::Folder: {
        // Filled yellow folder.
        QPainterPath back;
        back.moveTo(2.5, 6);
        back.lineTo(9, 6);
        back.lineTo(11, 8);
        back.lineTo(21.5, 8);
        back.lineTo(21.5, 19);
        back.lineTo(2.5, 19);
        back.closeSubpath();
        p.setPen(QPen(QColor(0xc8, 0x96, 0x1e), 1.0));
        p.setBrush(QColor(0xe8, 0xb4, 0x3a));
        p.drawPath(back);
        p.setBrush(QColor(0xf7, 0xcf, 0x5c));
        p.drawRect(QRectF(2.5, 10, 19, 9));
        break;
    }
    case Glyph::ChevronDown: {
        QPainterPath v;
        v.moveTo(6, 9.5);
        v.lineTo(12, 15.5);
        v.lineTo(18, 9.5);
        p.drawPath(v);
        break;
    }
    case Glyph::Menu:
        p.drawLine(QPointF(5, 7), QPointF(19, 7));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        p.drawLine(QPointF(5, 17), QPointF(19, 17));
        break;
    case Glyph::CopyPath: {
        // Two bars and an arrow pointing down: this folder, over there.
        p.drawLine(QPointF(5, 5.5), QPointF(19, 5.5));
        p.drawLine(QPointF(5, 10), QPointF(19, 10));
        QPainterPath tri;
        tri.moveTo(6.5, 14);
        tri.lineTo(17.5, 14);
        tri.lineTo(12, 20);
        tri.closeSubpath();
        p.setBrush(color);
        p.drawPath(tri);
        break;
    }
    case Glyph::Swap: {
        QPainterPath up;
        up.moveTo(7, 10);
        up.lineTo(12, 4.5);
        up.lineTo(17, 10);
        p.drawPath(up);
        QPainterPath down;
        down.moveTo(7, 14);
        down.lineTo(12, 19.5);
        down.lineTo(17, 14);
        p.drawPath(down);
        break;
    }
    case Glyph::Close:
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    }
    return pm;
}

QIcon icon(Glyph g, const QColor &color)
{
    // Made once per glyph and color: role changes restyle the bar often.
    static QHash<QPair<int, QRgb>, QIcon> cache;
    const auto key = qMakePair(int(g), color.rgba());
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return *it;
    QIcon i;
    for (int size : {16, 20, 24, 32, 48}) {
        i.addPixmap(pixmap(g, color, size, 1.0), QIcon::Normal);
        i.addPixmap(pixmap(g, color, size, 2.0), QIcon::Normal);
        QColor faded = color;
        faded.setAlphaF(0.4);
        i.addPixmap(pixmap(g, faded, size, 1.0), QIcon::Disabled);
        i.addPixmap(pixmap(g, faded, size, 2.0), QIcon::Disabled);
    }
    cache.insert(key, i);
    return i;
}

} // namespace Glyphs
