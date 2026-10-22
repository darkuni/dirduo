#pragma once

#include <QList>
#include <QWidget>

class QBoxLayout;
class QToolBar;

// The space between a lister's two displays, where toolbars can live. With the
// displays side by side the toolbars run vertically and stack in columns; with
// them top and bottom the toolbars run horizontally and stack in rows.
class ToolbarStrip : public QWidget
{
    Q_OBJECT
public:
    explicit ToolbarStrip(QWidget *parent = nullptr);

    // The splitter's orientation: Qt::Horizontal means displays side by side.
    void setSplitOrientation(Qt::Orientation splitOrientation);

    void addBar(QToolBar *bar, int index = -1);
    void removeBar(QToolBar *bar);
    bool contains(QToolBar *bar) const { return m_bars.contains(bar); }
    QList<QToolBar *> bars() const { return m_bars; }
    void moveBar(QToolBar *bar, int delta);

    // Recomputes the strip's thickness after a bar changed.
    void updateGeometryForBars();

private:
    void relayout();

    Qt::Orientation m_split = Qt::Horizontal;
    QBoxLayout *m_layout;
    QList<QToolBar *> m_bars;
};
