#pragma once

#include <QImage>
#include <QStringList>
#include <QWidget>

class QLabel;

// The picture viewer (Opus's standalone viewer): one picture at a time in its
// own window, fitted or at 100%, stepping through the folder's pictures (or
// the selected ones) with the arrow keys. F or F11 for full screen.
//   Left/Right, Page Up/Down, Space/Backspace  previous / next
//   Home / End     first / last
//   0 or *         fit / actual size;  + and -  zoom;  wheel zooms too
//   R / L          turn right / left (view only)
//   Escape         close (or leave full screen)
class ImageViewer : public QWidget
{
    Q_OBJECT
public:
    // Shows `start`, stepping through `pictures` (all of them in its folder
    // when empty).
    ImageViewer(const QString &start, const QStringList &pictures = {}, QWidget *parent = nullptr);

    QString current() const;
    int index() const { return m_index; }
    int count() const { return int(m_pictures.size()); }
    QSize imageSize() const { return m_image.size(); }
    double zoom() const { return m_zoom; } // 0: fit
    void step(int delta);
    void goTo(int index);

    static QStringList picturesIn(const QString &folder);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void load();
    void updateTitle();
    QRectF targetRect() const;

    QStringList m_pictures;
    int m_index = 0;
    QImage m_image;
    QString m_error;
    double m_zoom = 0; // 0: fit the window
    int m_rotate = 0;
    QPointF m_offset;
    QPoint m_dragFrom;
};
