#include "ui/MetadataPane.h"

#include "core/CommandEngine.h"
#include "core/Location.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

const char *const kNames[] = {"Title",  "Artist", "Album artist", "Album",       "Track",    "Disc",
                              "Year",   "Genre",  "Comment",      "Date taken",  "Description", "Copyright"};

constexpr const char *kSeveral = "(several values)";

QString textOf(const MediaInfo &m, int f)
{
    switch (f) {
    case MetadataPane::Title: return m.title;
    case MetadataPane::Artist: return m.artist;
    case MetadataPane::AlbumArtist: return m.albumArtist;
    case MetadataPane::Album: return m.album;
    case MetadataPane::Track: return m.track > 0 ? QString::number(m.track) : QString();
    case MetadataPane::Disc: return m.disc > 0 ? QString::number(m.disc) : QString();
    case MetadataPane::Year: return m.year > 0 ? QString::number(m.year) : QString();
    case MetadataPane::Genre: return m.genre;
    case MetadataPane::Comment: return m.comment;
    case MetadataPane::Taken: return m.taken.isValid() ? m.taken.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
    case MetadataPane::Description: return m.description;
    case MetadataPane::Copyright: return m.copyright;
    }
    return {};
}

bool isMusicField(int f)
{
    return f <= MetadataPane::Comment;
}

bool isPhotoField(int f)
{
    return f == MetadataPane::Artist || f >= MetadataPane::Taken;
}

} // namespace

MetadataPane::MetadataPane(CommandEngine &engine, QWidget *parent)
    : QDockWidget(QStringLiteral("Metadata"), parent)
    , m_engine(engine)
{
    setObjectName(QStringLiteral("metadata-pane"));
    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 4, 6, 6);
    m_heading = new QLabel(body);
    m_heading->setWordWrap(true);
    QFont hf = m_heading->font();
    hf.setBold(true);
    m_heading->setFont(hf);
    layout->addWidget(m_heading);

    auto *formHost = new QWidget(body);
    m_form = new QFormLayout(formHost);
    m_form->setContentsMargins(0, 0, 0, 0);
    for (int f = 0; f < FieldCount; ++f) {
        m_labels[f] = new QLabel(QString::fromLatin1(kNames[f]) + QLatin1Char(':'), formHost);
        m_edits[f] = new QLineEdit(formHost);
        m_edits[f]->setClearButtonEnabled(true);
        m_form->addRow(m_labels[f], m_edits[f]);
        connect(m_edits[f], &QLineEdit::textEdited, this, [this, f]() {
            m_touched[f] = true;
            QFont font = m_edits[f]->font();
            font.setItalic(true); // changed, not yet written
            m_edits[f]->setFont(font);
            updateButtons();
        });
        connect(m_edits[f], &QLineEdit::returnPressed, this, [this]() {
            if (hasChanges())
                apply();
        });
    }
    m_edits[Taken]->setPlaceholderText(QStringLiteral("2026-10-21 18:15:00"));
    m_edits[Track]->setToolTip(QStringLiteral("With several files selected, use Number Tracks (Metadata TRACK=auto)"));
    auto *scroll = new QScrollArea(body);
    scroll->setWidget(formHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll, 1);

    m_status = new QLabel(body);
    m_status->setWordWrap(true);
    m_status->setEnabled(false);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_revert = new QPushButton(QStringLiteral("Revert"), body);
    m_apply = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Apply"), body);
    m_apply->setDefault(true);
    buttons->addWidget(m_revert);
    buttons->addWidget(m_apply);
    layout->addLayout(buttons);
    // Wide enough for an album name without scrolling the field.
    body->setMinimumWidth(fontMetrics().averageCharWidth() * 40);
    setWidget(body);

    connect(m_apply, &QPushButton::clicked, this, &MetadataPane::apply);
    connect(m_revert, &QPushButton::clicked, this, &MetadataPane::revert);
    m_loadTimer.setSingleShot(true);
    m_loadTimer.setInterval(150);
    connect(&m_loadTimer, &QTimer::timeout, this, &MetadataPane::load);
    load();
}

void MetadataPane::setFiles(const QStringList &paths)
{
    if (paths == m_files && !hasChanges())
        return;
    m_files = paths;
    m_loadTimer.start(); // unsaved changes to the old selection are dropped
}

void MetadataPane::loadNow()
{
    m_loadTimer.stop();
    load();
}

bool MetadataPane::isFieldShown(Field f) const
{
    return !m_edits[f]->isHidden();
}

bool MetadataPane::hasChanges() const
{
    for (auto it = m_touched.cbegin(); it != m_touched.cend(); ++it) {
        if (it.value())
            return true;
    }
    return false;
}

QString MetadataPane::statusText() const
{
    return m_status->text();
}

void MetadataPane::load()
{
    m_loading = true;
    m_touched.clear();
    m_editable.clear();
    bool music = false, photo = false;
    QList<MediaInfo> infos;
    int other = 0;
    const bool tooMany = m_files.size() > kMaxFiles;
    if (!tooMany) {
        for (const QString &p : std::as_const(m_files)) {
            const MediaInfo::Kind k = Metadata::kindFor(p);
            if (!Location::isLocal(p) || k == MediaInfo::Kind::None || k == MediaInfo::Kind::Video) {
                ++other;
                continue;
            }
            music = music || k == MediaInfo::Kind::Audio;
            photo = photo || k == MediaInfo::Kind::Image;
            infos << Metadata::read(p);
            if (Metadata::canWrite(p))
                m_editable << p;
        }
    }
    for (int f = 0; f < FieldCount; ++f) {
        const bool show = (music && isMusicField(f)) || (photo && isPhotoField(f));
        m_labels[f]->setVisible(show);
        m_edits[f]->setVisible(show);
        QFont font = m_edits[f]->font();
        font.setItalic(false);
        m_edits[f]->setFont(font);
        // One value, or several.
        QString value;
        bool same = true;
        for (int i = 0; i < infos.size(); ++i) {
            const QString v = textOf(infos.at(i), f);
            if (i == 0)
                value = v;
            else if (v != value)
                same = false;
        }
        m_edits[f]->setText(same ? value : QString());
        m_edits[f]->setPlaceholderText(same ? (f == Taken ? QStringLiteral("2026-10-21 18:15:00") : QString())
                                            : QString::fromLatin1(kSeveral));
        m_edits[f]->setReadOnly(m_editable.isEmpty() || (f == Track && m_editable.size() > 1));
    }
    if (m_files.isEmpty())
        m_heading->setText(QStringLiteral("Nothing selected"));
    else if (m_files.size() == 1)
        m_heading->setText(Location::fileName(m_files.first()));
    else
        m_heading->setText(QStringLiteral("%1 files").arg(m_files.size()));

    QStringList notes;
    if (tooMany)
        notes << QStringLiteral("Select fewer than %1 files to see their tags.").arg(kMaxFiles);
    else if (infos.isEmpty() && !m_files.isEmpty())
        notes << QStringLiteral("No music or photos in the selection.");
    else if (!infos.isEmpty() && m_editable.isEmpty())
        notes << QStringLiteral("These tags can be read but not written here.");
    else if (m_editable.size() < int(infos.size()))
        notes << QStringLiteral("%1 of %2 can be written; the rest are left alone.").arg(m_editable.size()).arg(infos.size());
    if (other > 0 && !infos.isEmpty())
        notes << QStringLiteral("%1 other files are left alone.").arg(other);
    m_status->setText(notes.join(QLatin1Char(' ')));
    m_loading = false;
    updateButtons();
}

void MetadataPane::updateButtons()
{
    const bool dirty = hasChanges();
    m_apply->setEnabled(dirty && !m_editable.isEmpty());
    m_revert->setEnabled(dirty);
}

int MetadataPane::apply()
{
    if (!hasChanges() || m_editable.isEmpty())
        return 0;
    MediaEdit e;
    auto str = [this](Field f, std::optional<QString> &out) {
        if (m_touched.value(f))
            out = m_edits[f]->text().trimmed();
    };
    auto num = [this](Field f, std::optional<int> &out) -> bool {
        if (!m_touched.value(f))
            return true;
        const QString t = m_edits[f]->text().trimmed();
        bool ok = true;
        out = t.isEmpty() ? 0 : t.toInt(&ok);
        return ok && *out >= 0;
    };
    str(Title, e.title);
    str(Artist, e.artist);
    str(AlbumArtist, e.albumArtist);
    str(Album, e.album);
    str(Genre, e.genre);
    str(Comment, e.comment);
    str(Description, e.description);
    str(Copyright, e.copyright);
    if (!num(Track, e.track) || !num(Disc, e.disc) || !num(Year, e.year)) {
        QMessageBox::warning(this, QStringLiteral("Metadata"), QStringLiteral("Track, disc and year take a number."));
        return 0;
    }
    if (m_touched.value(Taken)) {
        const QString t = m_edits[Taken]->text().trimmed();
        QDateTime when;
        for (const QString &fmt : {QStringLiteral("yyyy-MM-dd HH:mm:ss"), QStringLiteral("yyyy-MM-dd HH:mm"), QStringLiteral("yyyy-MM-dd")}) {
            when = QDateTime::fromString(t, fmt);
            if (when.isValid())
                break;
        }
        if (!t.isEmpty() && !when.isValid()) {
            QMessageBox::warning(this, QStringLiteral("Metadata"), QStringLiteral("Date taken looks like 2026-10-21 18:15:00."));
            return 0;
        }
        e.taken = when;
    }
    QList<QPair<QString, MediaEdit>> edits;
    for (const QString &p : std::as_const(m_editable)) {
        // Photo fields only to photos, music fields only to music.
        MediaEdit one = e;
        if (Metadata::kindFor(p) == MediaInfo::Kind::Image) {
            one.title.reset();
            one.albumArtist.reset();
            one.album.reset();
            one.genre.reset();
            one.comment.reset();
            one.year.reset();
            one.track.reset();
            one.disc.reset();
        } else {
            one.taken.reset();
            one.description.reset();
            one.copyright.reset();
        }
        if (!one.isEmpty())
            edits << qMakePair(p, one);
    }
    QStringList problems;
    const QString what = e.fieldNames().join(QStringLiteral(", "));
    const int written = m_engine.applyTags(
        edits, QStringLiteral("Set %1 on %2").arg(what, edits.size() == 1 ? Location::fileName(edits.first().first)
                                                                           : QStringLiteral("%1 files").arg(edits.size())),
        &problems);
    if (!problems.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Metadata"), QStringLiteral("Not written:\n") + problems.join(QLatin1Char('\n')));
    QStringList changed;
    for (const auto &ed : std::as_const(edits))
        changed << ed.first;
    load(); // what is in the files now
    Q_EMIT applied(changed);
    return written;
}

void MetadataPane::revert()
{
    load();
}
