#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;
class QVideoWidget;
class QMediaPlayer;
class QAudioOutput;

// Plays music and video: the picture for video, the tags (and cover art, when
// the file has some) for music, play/pause, a position bar and, in the full
// version (the standalone viewer), volume and mute. The player itself is only
// made the first time something is loaded. Without Qt Multimedia it shows
// nothing and plays nothing.
class MediaPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MediaPanel(QWidget *parent, bool full = false);

    // Music and video can play here (built with Qt Multimedia, and with a
    // backend that can decode something).
    static bool available();

    void load(const QString &path, const QString &tags);
    void stop();
    void play();
    void pause();
    void playPause();
    void seekBy(qint64 ms);
    void setPosition(qint64 ms);
    qint64 position() const;
    qint64 duration() const;
    int volume() const; // 0..100
    void setVolume(int percent);
    bool isMuted() const;
    void setMuted(bool on);
    bool loaded() const;
    bool playing() const;
    bool hasVideo() const;
    QString source() const { return m_path; }

Q_SIGNALS:
    void ended();          // played to the end
    void stateChanged();   // playing, paused, stopped
    void failed(const QString &why);

private:
    void ensurePlayer();
    void updateTime();
    void updateVolumeIcon();

    QString m_path;
    bool m_full;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoWidget *m_video = nullptr;
    QLabel *m_cover = nullptr;
    QLabel *m_tags = nullptr;
    QToolButton *m_play = nullptr;
    QSlider *m_position = nullptr;
    QLabel *m_time = nullptr;
    QToolButton *m_mute = nullptr;
    QSlider *m_volume = nullptr;
    int m_volumePercent = 80;
    bool m_muted = false;
};
