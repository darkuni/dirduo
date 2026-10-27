#include "ui/MediaPanel.h"

#include "core/Metadata.h"

#ifdef DIRDUO_HAVE_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QVideoWidget>
#endif

#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

MediaPanel::MediaPanel(QWidget *parent, bool full)
    : QWidget(parent)
    , m_full(full)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
#ifdef DIRDUO_HAVE_MULTIMEDIA
    m_video = new QVideoWidget(this);
    m_video->setMinimumHeight(80);
    m_video->hide();
    layout->addWidget(m_video, 1);
#endif
    m_cover = new QLabel(this);
    m_cover->setAlignment(Qt::AlignCenter);
    m_cover->hide();
    layout->addWidget(m_cover, 2);
    m_tags = new QLabel(this);
    m_tags->setAlignment(Qt::AlignCenter);
    m_tags->setWordWrap(true);
    m_tags->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_tags, 1);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(full ? 8 : 0, 0, full ? 8 : 0, full ? 4 : 0);
    m_play = new QToolButton(this);
    m_play->setObjectName(QStringLiteral("media-play"));
    m_play->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    m_play->setText(QStringLiteral("Play"));
    m_play->setToolTip(QStringLiteral("Play / pause"));
    m_position = new QSlider(Qt::Horizontal, this);
    m_position->setObjectName(QStringLiteral("media-position"));
    m_time = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    row->addWidget(m_play);
    row->addWidget(m_position, 1);
    row->addWidget(m_time);
    if (full) {
        m_mute = new QToolButton(this);
        m_mute->setObjectName(QStringLiteral("media-mute"));
        m_mute->setToolTip(QStringLiteral("Mute"));
        m_mute->setCheckable(true);
        m_volume = new QSlider(Qt::Horizontal, this);
        m_volume->setObjectName(QStringLiteral("media-volume"));
        m_volume->setRange(0, 100);
        m_volume->setValue(m_volumePercent);
        m_volume->setFixedWidth(110);
        m_volume->setToolTip(QStringLiteral("Volume (Up / Down)"));
        row->addSpacing(8);
        row->addWidget(m_mute);
        row->addWidget(m_volume);
        connect(m_mute, &QToolButton::toggled, this, &MediaPanel::setMuted);
        connect(m_volume, &QSlider::valueChanged, this, &MediaPanel::setVolume);
        updateVolumeIcon();
    }
    layout->addLayout(row);
    connect(m_play, &QToolButton::clicked, this, &MediaPanel::playPause);
    connect(m_position, &QSlider::sliderMoved, this, [this](int v) { setPosition(qint64(v) * 100); });
}

bool MediaPanel::available()
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    // A media backend with no decoders (GStreamer without its plugins) can
    // crash when a player is made: check that it can decode something first.
    static const bool usable = !QMediaFormat().supportedFileFormats(QMediaFormat::Decode).isEmpty();
    return usable;
#else
    return false;
#endif
}

void MediaPanel::load(const QString &path, const QString &tags)
{
    stop();
    m_path = path;
    m_tags->setText(tags);
    m_cover->clear();
    m_cover->hide();
    if (m_full && Metadata::kindFor(path) == MediaInfo::Kind::Audio) {
        // Until the file's own cover art turns up.
        const int side = qBound(96, qMin(width(), height()) / 3, 256);
        m_cover->setPixmap(QIcon::fromTheme(QStringLiteral("audio-x-generic")).pixmap(side, side));
        m_cover->show();
    }
    if (m_video)
        m_video->hide();
    m_tags->show();
    m_position->setValue(0);
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (!available())
        return;
    ensurePlayer();
    m_player->setSource(QUrl::fromLocalFile(path));
#endif
}

void MediaPanel::stop()
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (!m_player)
        return;
    m_player->stop();
    m_player->setSource(QUrl());
#endif
    m_path.clear();
}

void MediaPanel::play()
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (m_player)
        m_player->play();
#endif
}

void MediaPanel::pause()
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (m_player)
        m_player->pause();
#endif
}

void MediaPanel::playPause()
{
    if (playing())
        pause();
    else
        play();
}

void MediaPanel::seekBy(qint64 ms)
{
    setPosition(qMax<qint64>(0, position() + ms));
}

void MediaPanel::setPosition(qint64 ms)
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (m_player) {
        if (m_player->duration() > 0)
            ms = qMin(ms, m_player->duration());
        m_player->setPosition(ms);
    }
#else
    Q_UNUSED(ms)
#endif
}

qint64 MediaPanel::position() const
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    return m_player ? m_player->position() : 0;
#else
    return 0;
#endif
}

qint64 MediaPanel::duration() const
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    return m_player ? m_player->duration() : 0;
#else
    return 0;
#endif
}

int MediaPanel::volume() const
{
    return m_volumePercent;
}

void MediaPanel::setVolume(int percent)
{
    m_volumePercent = qBound(0, percent, 100);
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (m_audio)
        m_audio->setVolume(float(m_volumePercent) / 100.0f);
#endif
    if (m_volume && m_volume->value() != m_volumePercent) {
        const QSignalBlocker block(m_volume);
        m_volume->setValue(m_volumePercent);
    }
    updateVolumeIcon();
}

bool MediaPanel::isMuted() const
{
    return m_muted;
}

void MediaPanel::setMuted(bool on)
{
    m_muted = on;
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (m_audio)
        m_audio->setMuted(on);
#endif
    if (m_mute && m_mute->isChecked() != on) {
        const QSignalBlocker block(m_mute);
        m_mute->setChecked(on);
    }
    updateVolumeIcon();
}

bool MediaPanel::loaded() const
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    return m_player && !m_player->source().isEmpty();
#else
    return false;
#endif
}

bool MediaPanel::playing() const
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
#else
    return false;
#endif
}

bool MediaPanel::hasVideo() const
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    return m_player && m_player->hasVideo();
#else
    return false;
#endif
}

void MediaPanel::ensurePlayer()
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    if (m_player)
        return;
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(float(m_volumePercent) / 100.0f);
    m_audio->setMuted(m_muted);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
        m_position->setRange(0, int(d / 100));
        updateTime();
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        if (!m_position->isSliderDown())
            m_position->setValue(int(p / 100));
        updateTime();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
        m_play->setIcon(QIcon::fromTheme(st == QMediaPlayer::PlayingState ? QStringLiteral("media-playback-pause")
                                                                           : QStringLiteral("media-playback-start")));
        Q_EMIT stateChanged();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::EndOfMedia)
            Q_EMIT ended();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &why) {
        Q_EMIT failed(why);
    });
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, [this](bool v) {
        m_video->setVisible(v);
        m_tags->setVisible(!v);
        if (v)
            m_cover->hide();
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, [this]() {
        if (!m_full || m_player->hasVideo())
            return;
        // Album art for music, when the file carries it.
        const QMediaMetaData md = m_player->metaData();
        QImage art = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
        if (art.isNull())
            art = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();
        if (art.isNull())
            return;
        const int side = qMax(120, qMin(width(), height()) / 2);
        m_cover->setPixmap(QPixmap::fromImage(art.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        m_cover->show();
    });
#endif
}

void MediaPanel::updateTime()
{
    auto fmt = [](qint64 ms) {
        const qint64 s = ms / 1000;
        return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    };
    m_time->setText(QStringLiteral("%1 / %2").arg(fmt(position()), fmt(duration())));
}

void MediaPanel::updateVolumeIcon()
{
    if (!m_mute)
        return;
    const char *name = m_muted || m_volumePercent == 0 ? "audio-volume-muted"
        : m_volumePercent < 34                         ? "audio-volume-low"
        : m_volumePercent < 67                         ? "audio-volume-medium"
                                                       : "audio-volume-high";
    m_mute->setIcon(QIcon::fromTheme(QString::fromLatin1(name)));
}
