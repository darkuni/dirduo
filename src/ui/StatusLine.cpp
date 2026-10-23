#include "ui/StatusLine.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>

namespace {

class BarGraph : public QWidget
{
public:
    explicit BarGraph(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("status-bar-graph"));
        setFixedWidth(64);
    }
    double fraction = 0;
    bool low = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int h = qMax(6, fontMetrics().height() * 6 / 10);
        const QRect r(0, (height() - h) / 2, width() - 1, h);
        QColor frame = palette().color(QPalette::WindowText);
        frame.setAlphaF(0.4);
        p.setPen(frame);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        QRect fill = r.adjusted(1, 1, 0, 0);
        fill.setWidth(int(fill.width() * fraction));
        p.fillRect(fill, low ? QColor(0xd0, 0x40, 0x30) : palette().color(QPalette::Highlight));
    }
};

QString iconName(const QString &name)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("folder"), QStringLiteral("folder")},     {QStringLiteral("dir"), QStringLiteral("folder")},
        {QStringLiteral("file"), QStringLiteral("text-x-generic")}, {QStringLiteral("drive"), QStringLiteral("drive-harddisk")},
        {QStringLiteral("info"), QStringLiteral("dialog-information")}, {QStringLiteral("hidden"), QStringLiteral("view-hidden")},
        {QStringLiteral("filter"), QStringLiteral("view-filter")}, {QStringLiteral("lock"), QStringLiteral("object-locked")}};
    return map.value(name, name);
}

} // namespace

StatusLine::StatusLine(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("status-line"));
    m_layout->setContentsMargins(6, 2, 6, 2);
    m_layout->setSpacing(2);
}

QWidget *StatusLine::makeWidget(const StatusFormat::Segment &s)
{
    if (s.kind == StatusFormat::Segment::Kind::Bar)
        return new BarGraph(this);
    auto *l = new QLabel(this);
    l->setTextInteractionFlags(Qt::NoTextInteraction);
    if (s.kind == StatusFormat::Segment::Kind::Text) {
        l->setTextFormat(Qt::RichText);
        l->setFont(m_font);
    }
    return l;
}

void StatusLine::updateWidget(QWidget *w, const StatusFormat::Segment &s)
{
    switch (s.kind) {
    case StatusFormat::Segment::Kind::Bar: {
        auto *b = static_cast<BarGraph *>(w);
        b->fraction = s.fraction;
        b->low = s.low;
        b->setToolTip(QStringLiteral("%1% used").arg(qRound(s.fraction * 100)));
        b->update();
        break;
    }
    case StatusFormat::Segment::Kind::Icon: {
        const int px = qMax(12, QFontMetrics(m_font).height());
        static_cast<QLabel *>(w)->setPixmap(QIcon::fromTheme(iconName(s.icon)).pixmap(px, px));
        break;
    }
    case StatusFormat::Segment::Kind::Text:
        static_cast<QLabel *>(w)->setText(s.html);
        break;
    }
}

void StatusLine::setSegments(const QList<StatusFormat::Segment> &segments)
{
    // Same shape as before: only the contents change (the usual case).
    bool same = segments.size() == m_segments.size();
    for (int i = 0; same && i < segments.size(); ++i)
        same = segments.at(i).kind == m_segments.at(i).kind && segments.at(i).right == m_segments.at(i).right;
    m_segments = segments;
    if (!same) {
        qDeleteAll(m_widgets);
        m_widgets.clear();
        while (m_layout->count() > 0)
            delete m_layout->takeAt(0);
        bool stretched = false;
        for (const auto &s : segments) {
            if (s.right && !stretched) {
                m_layout->addStretch(1);
                stretched = true;
            }
            QWidget *w = makeWidget(s);
            m_layout->addWidget(w);
            m_widgets << w;
        }
        if (!stretched)
            m_layout->addStretch(1);
    }
    for (int i = 0; i < segments.size(); ++i)
        updateWidget(m_widgets.at(i), segments.at(i));
}

void StatusLine::setMessage(const QString &text)
{
    StatusFormat::Segment s;
    s.html = text.toHtmlEscaped();
    setSegments({s});
}

QString StatusLine::leftText() const
{
    return StatusFormat::plainText(m_segments, false);
}

QString StatusLine::rightText() const
{
    return StatusFormat::plainText(m_segments, true);
}

void StatusLine::setTextFont(const QFont &f)
{
    m_font = f;
    for (QWidget *w : std::as_const(m_widgets))
        w->setFont(f);
    setSegments(QList<StatusFormat::Segment>(m_segments)); // icons follow the size
}

void StatusLine::setTextPalette(const QPalette &p)
{
    setPalette(p);
    for (QWidget *w : std::as_const(m_widgets))
        w->setPalette(p);
}
