#include "ui/TagsDialog.h"

#include "core/Location.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QIcon dotIcon(const QColor &c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    if (c.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(c);
        p.setPen(c.darker(130));
        p.drawEllipse(QRectF(1, 1, 12, 12));
    }
    return QIcon(pm);
}

} // namespace

TagsDialog::TagsDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
{
    for (const QString &p : paths)
        m_before << FileTags::read(p);
    setWindowTitle(paths.size() == 1 ? QStringLiteral("Tags - %1").arg(Location::fileName(paths.first()))
                                     : QStringLiteral("Tags - %1 items").arg(paths.size()));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    // Label: the shared one, or "(several)".
    m_label = new QComboBox(this);
    QStringList labels;
    for (const FileTagData &d : std::as_const(m_before))
        if (!labels.contains(d.label))
            labels << d.label;
    if (labels.size() > 1)
        m_label->addItem(QStringLiteral("(several labels)"), QStringLiteral("*several*"));
    m_label->addItem(QStringLiteral("None"), QString());
    for (const LabelDef &d : FileTags::labels())
        m_label->addItem(dotIcon(d.color), d.name, d.name);
    m_label->setCurrentIndex(labels.size() > 1 ? 0 : qMax(0, m_label->findData(labels.value(0))));
    form->addRow(QStringLiteral("&Label:"), m_label);

    // Rating: five stars; click the lit last one again for none.
    auto *starRow = new QHBoxLayout;
    starRow->setSpacing(0);
    QList<int> ratings;
    for (const FileTagData &d : std::as_const(m_before))
        if (!ratings.contains(d.rating))
            ratings << d.rating;
    m_ratingMixed = ratings.size() > 1;
    m_rating = ratings.value(0);
    for (int i = 1; i <= 5; ++i) {
        auto *b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setToolTip(QStringLiteral("%1 star%2").arg(i).arg(i == 1 ? QString() : QStringLiteral("s")));
        QFont f = b->font();
        f.setPointSizeF(f.pointSizeF() * 1.4);
        b->setFont(f);
        connect(b, &QToolButton::clicked, this, [this, i]() { setRating(m_rating == i && !m_ratingMixed ? 0 : i); });
        starRow->addWidget(b);
        m_stars << b;
    }
    auto *mixed = new QLabel(m_ratingMixed ? QStringLiteral("(several ratings)") : QString(), this);
    starRow->addWidget(mixed);
    starRow->addStretch(1);
    form->addRow(QStringLiteral("Rating:"), starRow);
    setRating(m_rating);
    m_ratingTouched = false;

    // Tags: checked on all, partly on some.
    m_tags = new QListWidget(this);
    QHash<QString, int> counts;
    for (const FileTagData &d : std::as_const(m_before))
        for (const QString &t : d.tags)
            counts[t]++;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        addTag(it.key(), it.value() == m_before.size() ? Qt::Checked : Qt::PartiallyChecked);
    for (const QString &t : FileTags::knownTags())
        if (!counts.contains(t))
            addTag(t, Qt::Unchecked);
    m_tags->sortItems();
    m_tags->setMinimumHeight(fontMetrics().height() * 8);
    form->addRow(QStringLiteral("&Tags:"), m_tags);
    auto *newRow = new QHBoxLayout;
    m_newTag = new QLineEdit(this);
    m_newTag->setPlaceholderText(QStringLiteral("New tag (comma between several)"));
    m_newTag->setCompleter(new QCompleter(FileTags::knownTags(), m_newTag));
    auto *add = new QPushButton(QStringLiteral("&Add"), this);
    newRow->addWidget(m_newTag, 1);
    newRow->addWidget(add);
    form->addRow(QString(), newRow);
    auto addTyped = [this]() {
        for (const QString &t : m_newTag->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString tag = t.trimmed();
            if (tag.isEmpty())
                continue;
            const QList<QListWidgetItem *> found = m_tags->findItems(tag, Qt::MatchFixedString);
            if (found.isEmpty()) {
                addTag(tag, Qt::Checked);
                m_tagStart.insert(tag, Qt::Unchecked); // new: none of them has it yet
            }
            else
                found.first()->setCheckState(Qt::Checked);
        }
        m_newTag->clear();
    };
    connect(add, &QPushButton::clicked, this, addTyped);
    connect(m_newTag, &QLineEdit::returnPressed, this, addTyped);

    // Description.
    m_comment = new QPlainTextEdit(this);
    QStringList comments;
    for (const FileTagData &d : std::as_const(m_before))
        if (!comments.contains(d.comment))
            comments << d.comment;
    if (comments.size() == 1)
        m_comment->setPlainText(comments.first());
    else
        m_comment->setPlaceholderText(QStringLiteral("(several descriptions; type to give them all this one)"));
    m_comment->setFixedHeight(fontMetrics().height() * 4);
    connect(m_comment, &QPlainTextEdit::textChanged, this, [this]() { m_commentTouched = true; });
    form->addRow(QStringLiteral("&Description:"), m_comment);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setMinimumWidth(fontMetrics().averageCharWidth() * 56);
}

void TagsDialog::addTag(const QString &tag, Qt::CheckState state)
{
    auto *it = new QListWidgetItem(tag, m_tags);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable | (state == Qt::PartiallyChecked ? Qt::ItemIsAutoTristate : Qt::NoItemFlags));
    it->setCheckState(state);
    m_tagStart.insert(tag, state);
}

void TagsDialog::setRating(int stars)
{
    m_rating = qBound(0, stars, 5);
    m_ratingTouched = true;
    m_ratingMixed = false;
    for (int i = 0; i < m_stars.size(); ++i)
        m_stars.at(i)->setText(i < m_rating ? QString(QChar(0x2605)) : QString(QChar(0x2606)));
}

QList<QPair<QString, FileTagData>> TagsDialog::changes() const
{
    QList<QPair<QString, FileTagData>> out;
    const QString label = m_label->currentData().toString();
    for (int i = 0; i < m_paths.size(); ++i) {
        FileTagData d = m_before.at(i);
        if (label != QLatin1String("*several*"))
            d.label = label;
        if (m_ratingTouched)
            d.rating = m_rating;
        for (int r = 0; r < m_tags->count(); ++r) {
            const QListWidgetItem *it = m_tags->item(r);
            const QString tag = it->text();
            const Qt::CheckState now = it->checkState();
            if (now == m_tagStart.value(tag, Qt::Unchecked) || now == Qt::PartiallyChecked)
                continue;
            if (now == Qt::Checked && !d.tags.contains(tag))
                d.tags << tag;
            else if (now == Qt::Unchecked)
                d.tags.removeAll(tag);
        }
        if (m_commentTouched)
            d.comment = m_comment->toPlainText().trimmed();
        out << qMakePair(m_paths.at(i), d);
    }
    return out;
}
