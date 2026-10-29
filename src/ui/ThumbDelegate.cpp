#include "ui/ThumbDelegate.h"

#include "core/FileTags.h"
#include "core/Prefs.h"
#include "core/Thumbnails.h"
#include "ui/FileListModel.h"
#include "ui/Style.h"

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QTextLayout>

#include <cmath>

namespace {

const Prefs::Values::Thumbs &prefs()
{
    return Prefs::instance()->values().thumbs;
}

int padFor(int px)
{
    return qBound(4, px / 24, 8);
}

QFont smallFont(const QFont &f)
{
    QFont s = f;
    if (f.pointSizeF() > 0)
        s.setPointSizeF(qMax(6.0, f.pointSizeF() * 0.85));
    else
        s.setPixelSize(qMax(8, int(f.pixelSize() * 0.85)));
    return s;
}

// A smooth copy at this size, kept between paints (the slider redraws a lot).
QPixmap scaledPixmap(const QImage &img, const QSize &size, qreal dpr)
{
    const QSize device = (QSizeF(size) * dpr).toSize();
    const QString key = QStringLiteral("dd-thumb-%1-%2x%3").arg(img.cacheKey()).arg(device.width()).arg(device.height());
    QPixmap pm;
    if (!QPixmapCache::find(key, &pm)) {
        pm = QPixmap::fromImage(img.size() == device ? img : img.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        QPixmapCache::insert(key, pm);
    }
    return pm;
}

void softShadow(QPainter *p, const QRectF &r, qreal radius, int depth)
{
    p->save();
    p->setPen(Qt::NoPen);
    for (int i = depth; i > 0; --i) {
        p->setBrush(QColor(0, 0, 0, 16));
        p->drawRoundedRect(r.adjusted(-i, -i + depth * 0.6, i, i + depth * 0.6), radius + i, radius + i);
    }
    p->restore();
}

} // namespace

QSize ThumbDelegate::cellSize(int px, const QFont &font)
{
    const Prefs::Values::Thumbs &t = prefs();
    const int pad = padFor(px);
    const QFontMetrics fm(font);
    const QFontMetrics sm(smallFont(font));
    int label = t.labelLines * fm.lineSpacing();
    if (t.labelDetails)
        label += sm.lineSpacing();
    if (t.labelDate)
        label += sm.lineSpacing();
    if (label > 0)
        label += 4;
    return QSize(px + 2 * pad + t.spacing, pad + px + label + pad + t.spacing);
}

QRect ThumbDelegate::imageRect(const QRect &cell) const
{
    const int sp = prefs().spacing;
    const QRect content = cell.adjusted(sp / 2, sp / 2, -(sp - sp / 2), -(sp - sp / 2));
    const int pad = padFor(m_px);
    return QRect(content.center().x() - m_px / 2 + 1, content.top() + pad, m_px, m_px);
}

QRect ThumbDelegate::labelRect(const QRect &cell) const
{
    const int sp = prefs().spacing;
    const QRect content = cell.adjusted(sp / 2, sp / 2, -(sp - sp / 2), -(sp - sp / 2));
    const QRect box = imageRect(cell);
    return QRect(content.left() + 3, box.bottom() + 5, content.width() - 6, qMax(0, content.bottom() - box.bottom() - 5));
}

QSize ThumbDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    return cellSize(m_px, option.font);
}

void ThumbDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const
{
    QRect r = labelRect(option.rect);
    r.setHeight(qMax(r.height() / 2, editor->sizeHint().height()));
    r.setLeft(option.rect.left() + 2);
    r.setRight(option.rect.right() - 2);
    editor->setGeometry(r);
}

QStringList ThumbDelegate::detailLines(const QModelIndex &index)
{
    const Prefs::Values::Thumbs &t = prefs();
    QStringList lines;
    if (t.labelDetails) {
        QStringList parts;
        const QSize full = index.data(FileListModel::ThumbFullRole).toSize();
        if (full.isValid())
            parts << QStringLiteral("%1 × %2").arg(full.width()).arg(full.height());
        const QString size = index.siblingAtColumn(FileListModel::SizeColumn).data(Qt::DisplayRole).toString();
        if (!size.isEmpty() && !index.data(FileListModel::IsDirRole).toBool())
            parts << size;
        lines << parts.join(QStringLiteral(" - "));
    }
    if (t.labelDate) {
        const QDateTime when = index.data(FileListModel::LabelDateRole).toDateTime();
        lines << (when.isValid() ? QLocale().toString(when, QLocale::ShortFormat) : QString());
    }
    return lines;
}

void ThumbDelegate::drawPicture(QPainter *p, const QRect &boxIn, const QImage &img, const QModelIndex &index, bool video) const
{
    const Prefs::Values::Thumbs &t = prefs();
    const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
    const QRectF box = boxIn;
    const bool fill = t.resize == 1;
    const QSize full = index.data(FileListModel::ThumbFullRole).toSize();

    // Where the frame and the picture go.
    QRectF card; // photo print / slide mount
    QRectF window = box;
    if (t.frame == 3) {
        const qreal m = box.width() * 0.05;
        window = box.adjusted(m, m, -m, -m * 3);
    } else if (t.frame == 4) {
        // A 35mm mount: a 3:2 window, turned for pictures taller than wide.
        card = box.adjusted(1, 1, -1, -1);
        const qreal longSide = card.width() * 0.76;
        const bool tall = img.height() > img.width();
        const QSizeF win = tall ? QSizeF(longSide * 2 / 3, longSide) : QSizeF(longSide, longSide * 2 / 3);
        window = QRectF(card.center().x() - win.width() / 2, card.center().y() - win.height() / 2, win.width(), win.height());
    } else if (t.frame == 2) {
        window = box.adjusted(3, 2, -3, -5);
    }
    QRectF pic;
    QRect src = img.rect();
    if (fill) {
        pic = window;
        const QSizeF exp = QSizeF(img.size()).scaled(window.size(), Qt::KeepAspectRatioByExpanding);
        const qreal sx = img.width() / exp.width();
        const qreal sy = img.height() / exp.height();
        src = QRectF((exp.width() - window.width()) / 2 * sx, (exp.height() - window.height()) / 2 * sy, window.width() * sx,
                     window.height() * sy)
                  .toRect();
    } else {
        QSizeF s = QSizeF(img.size()).scaled(window.size(), Qt::KeepAspectRatio);
        // Tiny pictures (icons, sprites) stay their own size.
        if (full.isValid() && full.width() <= s.width() && full.height() <= s.height())
            s = full;
        pic = QRectF(window.center().x() - s.width() / 2, window.center().y() - s.height() / 2, s.width(), s.height());
        if (t.frame == 4)
            pic.moveCenter(window.center());
        else if (t.frame != 3)
            pic.moveBottom(window.bottom()); // a row of them stands on one line
    }
    pic = QRectF(pic.toRect());
    if (t.frame == 3) {
        // The print stands on the bottom of its square, next to the name.
        const qreal m = box.width() * 0.05;
        card = pic.adjusted(-m, -m, m, m * 3);
        const qreal dy = box.bottom() - 1 - card.bottom();
        card.translate(0, dy);
        pic.translate(0, dy);
    }

    // The frame.
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    switch (t.frame) {
    case 1:
        break; // drawn over the picture
    case 2:
        softShadow(p, pic, 3, 4);
        break;
    case 3:
        softShadow(p, card, 2, 4);
        p->setPen(QPen(QColor(0, 0, 0, 40), 1));
        p->setBrush(QColor(0xfb, 0xfa, 0xf6));
        p->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
        break;
    case 4: {
        softShadow(p, card, card.width() * 0.05, 3);
        p->setPen(QPen(QColor(0, 0, 0, 60), 1));
        p->setBrush(QColor(0xee, 0xe9, 0xdc));
        const qreal r = card.width() * 0.05;
        p->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), r, r);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(20, 20, 20));
        p->drawRoundedRect(window.adjusted(-1, -1, 1, 1), 2, 2);
        break;
    }
    default:
        break;
    }
    p->restore();

    // The picture.
    const QImage part = src == img.rect() ? img : img.copy(src);
    p->drawPixmap(pic.topLeft(), scaledPixmap(part, pic.size().toSize(), dpr));

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    if (t.frame == 1 || t.frame == 2) {
        p->setPen(QPen(t.frame == 1 ? Style::instance()->theme().dimText : QColor(0, 0, 0, 70), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(pic.adjusted(0.5, 0.5, -0.5, -0.5));
    }

    // Film edges on videos.
    if (video && t.sprockets && pic.width() > 24) {
        const qreal s = qMax(5.0, pic.width() * 0.07);
        const qreal hole = s * 0.55;
        p->setPen(Qt::NoPen);
        for (const QRectF strip : {QRectF(pic.left(), pic.top(), s, pic.height()), QRectF(pic.right() - s, pic.top(), s, pic.height())}) {
            p->setBrush(QColor(0, 0, 0, 210));
            p->drawRect(strip);
            p->setBrush(QColor(255, 255, 255, 200));
            for (qreal y = strip.top() + s * 0.35; y + hole * 0.75 < strip.bottom(); y += s * 1.1)
                p->drawRoundedRect(QRectF(strip.center().x() - hole / 2, y, hole, hole * 0.75), 1.2, 1.2);
        }
    }

    // The rating.
    const int rating = index.data(FileListModel::RatingRole).toInt();
    if (t.stars && rating > 0) {
        QFont f = p->font();
        f.setPixelSize(qBound(8, int(pic.width() / 11), 16));
        const QString stars = QString(rating, QChar(0x2605));
        const QFontMetrics fm(f);
        const QRectF pill(pic.left() + 4, pic.bottom() - fm.height() - 6, fm.horizontalAdvance(stars) + 8, fm.height() + 2);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 150));
        p->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        p->setFont(f);
        p->setPen(QColor(0xff, 0xc8, 0x3d));
        p->drawText(pill, Qt::AlignCenter, stars);
    }

    // The type's icon.
    if (t.typeIcon) {
        const int s = qBound(14, int(pic.width() / 6), 32);
        const QIcon icon = index.data(FileListModel::TypeIconRole).value<QIcon>();
        icon.paint(p, QRect(int(pic.right()) - s - 3, int(pic.bottom()) - s - 3, s, s));
    }

    // How big it is next to the biggest picture here.
    if (t.dimensionBars && full.isValid()) {
        const auto *model = qobject_cast<const FileListModel *>(index.model());
        qint64 max = 0;
        if (model) {
            max = model->maxPictureArea();
        } else if (const auto *proxy = qobject_cast<const QAbstractProxyModel *>(index.model())) {
            if (const auto *m = qobject_cast<const FileListModel *>(proxy->sourceModel()))
                max = m->maxPictureArea();
        }
        if (max > 0) {
            const qreal share = std::sqrt(double(qint64(full.width()) * full.height()) / double(max));
            const QRectF track(pic.left() + 4, pic.bottom() - 7, pic.width() - 8, 4);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 0, 0, 120));
            p->drawRoundedRect(track, 2, 2);
            p->setBrush(Style::instance()->theme().highlight);
            p->drawRoundedRect(QRectF(track.left(), track.top(), qMax(4.0, track.width() * qMin(1.0, share)), track.height()), 2, 2);
        }
    }
    p->restore();
}

void ThumbDelegate::drawIcon(QPainter *p, const QRect &box, const QModelIndex &index, const QImage &collage) const
{
    const QIcon icon = index.data(FileListModel::TypeIconRole).value<QIcon>();
    if (collage.isNull()) {
        const int s = qMax(16, int(box.width() * 0.62));
        icon.paint(p, QRect(box.center().x() - s / 2 + 1, box.bottom() - s - box.height() / 10, s, s));
        return;
    }
    // A folder with its pictures showing inside.
    icon.paint(p, box);
    const QRect body = box.adjusted(box.width() * 0.17, box.height() * 0.33, -box.width() * 0.17, -box.height() * 0.17);
    const QSize s = collage.size().scaled(body.size(), Qt::KeepAspectRatio);
    const QRect r(body.center().x() - s.width() / 2 + 1, body.center().y() - s.height() / 2 + 1, s.width(), s.height());
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    softShadow(p, r, 3, 2);
    p->restore();
    const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
    p->drawPixmap(r.topLeft(), scaledPixmap(collage, r.size(), dpr));
}

void ThumbDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const Prefs::Values::Thumbs &t = prefs();
    const Theme &theme = Style::instance()->theme();
    const int sp = t.spacing;
    const QRect cell = option.rect.adjusted(sp / 2, sp / 2, -(sp - sp / 2), -(sp - sp / 2));
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const bool focus = option.state & QStyle::State_HasFocus;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Selected, focused or under the pointer.
    const QRectF bg = QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5);
    if (selected) {
        QColor c = theme.highlight;
        c.setAlphaF(0.30);
        painter->setPen(QPen(theme.highlight, 1.5));
        painter->setBrush(c);
        painter->drawRoundedRect(bg, 5, 5);
    } else if (hover) {
        QColor c = theme.text;
        c.setAlphaF(0.07);
        painter->setPen(Qt::NoPen);
        painter->setBrush(c);
        painter->drawRoundedRect(bg, 5, 5);
    }
    if (focus && !selected) {
        painter->setPen(QPen(theme.highlight, 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(bg, 5, 5);
    }

    const QRect box = imageRect(option.rect);
    const QImage img = index.data(FileListModel::ThumbImageRole).value<QImage>();
    const bool isDir = index.data(FileListModel::IsDirRole).toBool();
    const auto kind = Thumbnails::Kind(index.data(FileListModel::ThumbKindRole).toInt());
    if (!img.isNull() && !isDir)
        drawPicture(painter, box, img, index, kind == Thumbnails::Kind::Video);
    else
        drawIcon(painter, box, index, isDir ? img : QImage());

    // The label.
    if (t.labelLines > 0 || t.labelDetails || t.labelDate) {
        const QRect lr = labelRect(option.rect);
        QColor fg = theme.text;
        const QVariant fgRole = index.data(Qt::ForegroundRole);
        if (fgRole.canConvert<QBrush>() && fgRole.value<QBrush>().style() != Qt::NoBrush)
            fg = fgRole.value<QBrush>().color();
        else if (fgRole.canConvert<QColor>() && fgRole.value<QColor>().isValid())
            fg = fgRole.value<QColor>();
        QFont f = option.font;
        const QVariant fontRole = index.data(Qt::FontRole);
        if (fontRole.canConvert<QFont>())
            f = fontRole.value<QFont>().resolve(option.font);
        int y = lr.top();
        if (t.labelLines > 0) {
            const QString name = index.data(Qt::DisplayRole).toString();
            const QFontMetrics fm(f);
            QTextLayout layout(name, f);
            QTextOption to(Qt::AlignHCenter);
            to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(to);
            layout.beginLayout();
            QStringList lines;
            while (lines.size() < t.labelLines) {
                QTextLine line = layout.createLine();
                if (!line.isValid())
                    break;
                line.setLineWidth(lr.width());
                if (lines.size() == t.labelLines - 1) {
                    lines << fm.elidedText(name.mid(line.textStart()), Qt::ElideMiddle, lr.width());
                    break;
                }
                lines << name.mid(line.textStart(), line.textLength()).trimmed();
            }
            layout.endLayout();
            painter->setFont(f);
            painter->setPen(fg);
            for (const QString &l : std::as_const(lines)) {
                painter->drawText(QRect(lr.left(), y, lr.width(), fm.lineSpacing()), Qt::AlignHCenter | Qt::AlignTop, l);
                y += fm.lineSpacing();
            }
        }
        const QStringList extra = detailLines(index);
        if (!extra.isEmpty()) {
            const QFont sf = smallFont(option.font);
            const QFontMetrics sm(sf);
            painter->setFont(sf);
            painter->setPen(theme.dimText);
            for (const QString &l : extra) {
                painter->drawText(QRect(lr.left(), y, lr.width(), sm.lineSpacing()), Qt::AlignHCenter | Qt::AlignTop,
                                  sm.elidedText(l, Qt::ElideRight, lr.width()));
                y += sm.lineSpacing();
            }
        }
    }
    painter->restore();
}
