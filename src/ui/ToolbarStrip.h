#pragma once

#include <QList>
#include <QPoint>
#include <QPointer>
#include <QWidget>

class QBoxLayout;
class QToolBar;

// The space between a lister's two displays, where toolbars can live. With the
// displays side by side the toolbars run vertically in columns; with them top
// and bottom they run horizontally in rows. A row (or column) can hold several
// toolbars one after another, as in Opus: the drives, then your own buttons.
// Drag a toolbar by an empty spot to move it to another row, or onto a new one.
class ToolbarStrip : public QWidget
{
    Q_OBJECT
public:
    explicit ToolbarStrip(QWidget *parent = nullptr);

    // The splitter's orientation: Qt::Horizontal means displays side by side.
    void setSplitOrientation(Qt::Orientation splitOrientation);

    // Adds a bar on a line of its own (at that line index, else last), or at
    // the end of the last line when joinLast.
    void addBar(QToolBar *bar, int line = -1, bool joinLast = false);
    void removeBar(QToolBar *bar);
    bool contains(QToolBar *bar) const;
    QList<QToolBar *> bars() const; // in order, line by line
    QList<QList<QToolBar *>> lines() const { return m_lines; }
    // Whether the bar shares the line of the one before it.
    bool isJoined(QToolBar *bar) const;
    void setJoined(QToolBar *bar, bool joined);
    // Earlier or later: within its line when it shares one, else the line moves.
    void moveBar(QToolBar *bar, int delta);
    bool canMove(QToolBar *bar, int delta) const;
    // Puts the bar at that place: line, position in it; newLine inserts a line there.
    void placeBar(QToolBar *bar, int line, int position, bool newLine);

    // Lays the bars out again (a bar gained or lost a spacer).
    void refreshLayout() { relayout(); }
    // Recomputes the strip's thickness after a bar changed.
    void updateGeometryForBars();

Q_SIGNALS:
    void arrangementChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Drop
    {
        int line = -1;
        int position = 0;
        bool newLine = false;
        bool valid = false;
    };
    void relayout();
    void prune();
    Drop dropAt(const QPoint &pos, QToolBar *dragged) const;
    void showIndicator(const Drop &d);
    QRect lineRect(int line) const;
    bool sideBySide() const { return m_split == Qt::Horizontal; }

    Qt::Orientation m_split = Qt::Horizontal;
    QBoxLayout *m_layout;
    QList<QList<QToolBar *>> m_lines;
    QPointer<QToolBar> m_pressBar;
    QPoint m_pressPos;
    bool m_dragging = false;
    QWidget *m_indicator;
};
