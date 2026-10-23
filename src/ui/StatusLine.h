#pragma once

#include "core/StatusFormat.h"

#include <QWidget>

class QHBoxLayout;

// A display's status bar: the segments of its definition, text left and
// right, with icons and a bar graph of the drive.
class StatusLine : public QWidget
{
    Q_OBJECT
public:
    explicit StatusLine(QWidget *parent = nullptr);

    void setSegments(const QList<StatusFormat::Segment> &segments);
    // Plain text, for messages ("Reading folder...") that are not a definition.
    void setMessage(const QString &text);
    QString leftText() const;
    QString rightText() const;
    void setTextFont(const QFont &f);
    void setTextPalette(const QPalette &p);

private:
    QWidget *makeWidget(const StatusFormat::Segment &s);
    void updateWidget(QWidget *w, const StatusFormat::Segment &s);

    QHBoxLayout *m_layout;
    QList<QWidget *> m_widgets;
    QList<StatusFormat::Segment> m_segments;
    QFont m_font;
};
