#include "ui/ImageViewer.h"

#include "core/FileOps.h"
#include "core/ImageConvert.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLocale>
#include <QPainter>
#include <QScreen>
#include <QTransform>

#include <algorithm>

ImageViewer::ImageViewer(const QString &start, const QStringList &pictures, QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("imageViewer"));
    m_pictures = pictures.isEmpty() ? picturesIn(QFileInfo(start).absolutePath()) : pictures;
    m_index = qMax(0, int(m_pictures.indexOf(start)));
    if (!m_pictures.contains(start)) {
        m_pictures.prepend(start);
        m_index = 0;
    }
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x18, 0x18, 0x18));
    setPalette(pal);
    setAutoFillBackground(true);
    if (QScreen *s = screen())
        resize(s->availableSize() * 0.75);
    load();
}

QStringList ImageViewer::picturesIn(const QString &folder)
{
    QStringList out;
    const QStringList names = QDir(folder).entryList(QDir::Files, QDir::NoSort);
    for (const QString &n : names) {
        const QString p = QDir(folder).filePath(n);
        if (ImageConvert::readableFormats().contains(QFileInfo(n).suffix().toLower()))
            out << p;
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return FileOps::naturalCompare(a, b) < 0; });
    return out;
}

QString ImageViewer::current() const
{
    return m_pictures.value(m_index);
}

void ImageViewer::step(int delta)
{
    if (m_pictures.isEmpty())
        return;
    goTo(qBound(0, m_index + delta, int(m_pictures.size()) - 1));
}

void ImageViewer::goTo(int index)
{
    if (index < 0 || index >= m_pictures.size() || (index == m_index && !m_image.isNull()))
        return;
    m_index = index;
    load();
}

void ImageViewer::load()
{
    QImageReader reader(current());
    reader.setAutoTransform(true);
    m_image = reader.read();
    m_error = m_image.isNull() ? reader.errorString() : QString();
    m_zoom = 0;
    m_rotate = 0;
    m_offset = {};
    updateTitle();
    update();
}

void ImageViewer::updateTitle()
{
    const QFileInfo fi(current());
    QString t = QStringLiteral("%1 (%2 of %3)").arg(fi.fileName()).arg(m_index + 1).arg(m_pictures.size());
    if (!m_image.isNull())
        t += QStringLiteral(" - %1 x %2").arg(m_image.width()).arg(m_image.height());
    if (m_zoom > 0)
        t += QStringLiteral(" - %1%").arg(qRound(m_zoom * 100));
    setWindowTitle(t);
}

QRectF ImageViewer::targetRect() const
{
    QSizeF s = m_image.size();
    if (m_rotate % 180 != 0)
        s.transpose();
    double scale = m_zoom;
    if (scale <= 0) {
        // Fit: shrink to the window, never blow small pictures up.
        scale = qMin(1.0, qMin(width() / qMax(1.0, s.width()), height() / qMax(1.0, s.height())));
    }
    const QSizeF shown = s * scale;
    return QRectF(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2) + m_offset, shown);
}

void ImageViewer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_image.isNull()) {
        p.setPen(Qt::lightGray);
        p.drawText(rect(), Qt::AlignCenter, m_error.isEmpty() ? QStringLiteral("No picture") : m_error);
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = targetRect();
    p.translate(r.center());
    p.rotate(m_rotate);
    QSizeF s = r.size();
    if (m_rotate % 180 != 0)
        s.transpose();
    p.drawImage(QRectF(QPointF(-s.width() / 2, -s.height() / 2), s), m_image);
}

void ImageViewer::keyPressEvent(QKeyEvent *e)
{
    auto zoomTo = [this](double z) {
        m_zoom = qBound(0.02, z, 32.0);
        updateTitle();
        update();
    };
    const double current = m_zoom > 0 ? m_zoom : targetRect().width() / qMax(1, m_rotate % 180 ? m_image.height() : m_image.width());
    switch (e->key()) {
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        step(1);
        return;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        step(-1);
        return;
    case Qt::Key_Home:
        goTo(0);
        return;
    case Qt::Key_End:
        goTo(int(m_pictures.size()) - 1);
        return;
    case Qt::Key_0:
        m_zoom = 0;
        m_offset = {};
        updateTitle();
        update();
        return;
    case Qt::Key_Asterisk:
    case Qt::Key_1:
        zoomTo(1.0);
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomTo(current * 1.25);
        return;
    case Qt::Key_Minus:
        zoomTo(current / 1.25);
        return;
    case Qt::Key_R:
        m_rotate = (m_rotate + 90) % 360;
        update();
        return;
    case Qt::Key_L:
        m_rotate = (m_rotate + 270) % 360;
        update();
        return;
    case Qt::Key_F:
    case Qt::Key_F11:
        setWindowState(windowState() ^ Qt::WindowFullScreen);
        return;
    case Qt::Key_Escape:
        if (windowState() & Qt::WindowFullScreen)
            setWindowState(windowState() & ~Qt::WindowFullScreen);
        else
            close();
        return;
    default:
        QWidget::keyPressEvent(e);
    }
}

void ImageViewer::wheelEvent(QWheelEvent *e)
{
    // The wheel zooms; with Ctrl it steps through the pictures.
    const int dy = e->angleDelta().y();
    if (dy == 0)
        return;
    if (e->modifiers() & Qt::ControlModifier) {
        step(dy > 0 ? -1 : 1);
        return;
    }
    const double current = m_zoom > 0 ? m_zoom : targetRect().width() / qMax(1, m_rotate % 180 ? m_image.height() : m_image.width());
    m_zoom = qBound(0.02, dy > 0 ? current * 1.15 : current / 1.15, 32.0);
    updateTitle();
    update();
}

void ImageViewer::mousePressEvent(QMouseEvent *e)
{
    m_dragFrom = e->position().toPoint();
    if (e->button() == Qt::BackButton)
        step(-1);
    else if (e->button() == Qt::ForwardButton)
        step(1);
}

void ImageViewer::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton))
        return;
    const QPoint now = e->position().toPoint();
    m_offset += now - m_dragFrom; // drag to pan
    m_dragFrom = now;
    update();
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent *)
{
    setWindowState(windowState() ^ Qt::WindowFullScreen);
}
