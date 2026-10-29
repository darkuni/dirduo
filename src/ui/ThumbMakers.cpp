#include "ui/ThumbMakers.h"

#include "core/Prefs.h"
#include "core/Thumbnails.h"
#include "ui/PdfDoc.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#ifdef DIRDUO_HAVE_MULTIMEDIA
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif

#include <memory>

namespace {

void syncOptions()
{
    const Prefs::Values::Thumbs &t = Prefs::instance()->values().thumbs;
    Thumbnails::Options o;
    o.video = t.video;
    o.documents = t.documents;
    o.covers = t.covers;
    ThumbnailCache::instance()->setOptions(o);
}

#ifdef DIRDUO_HAVE_MULTIMEDIA
// Plays the video silently, a tenth of the way in (at most 30 s), and keeps
// the first frame from there. Gives up after a few seconds with whatever
// frame it has.
void grabVideoFrame(const QString &path, int size, std::function<void(const QImage &)> done)
{
    struct State
    {
        QPointer<QMediaPlayer> player;
        QPointer<QVideoSink> sink;
        qint64 target = 0; // ms
        QImage last;
        bool finished = false;
        std::function<void(const QImage &)> done;
        int size = 512;
    };
    auto st = std::make_shared<State>();
    st->done = std::move(done);
    st->size = size;
    auto *player = new QMediaPlayer(qApp);
    auto *sink = new QVideoSink(player);
    st->player = player;
    st->sink = sink;
    player->setVideoOutput(sink);
    auto finish = [st](QImage img) {
        if (st->finished)
            return;
        st->finished = true;
        if (img.isNull())
            img = st->last; // the cache scales it, and keeps the frame's own size
        if (st->player) {
            st->player->stop();
            st->player->deleteLater();
        }
        QTimer::singleShot(0, qApp, [st, img]() { st->done(img); });
    };
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, player, [st, finish](QMediaPlayer::MediaStatus s) {
        if (!st->player)
            return;
        if (s == QMediaPlayer::LoadedMedia) {
            const qint64 d = st->player->duration();
            st->target = d > 0 ? qMin<qint64>(d / 10, 30000) : 0;
            if (st->target > 0)
                st->player->setPosition(st->target);
            st->player->play();
        } else if (s == QMediaPlayer::InvalidMedia || s == QMediaPlayer::EndOfMedia) {
            finish({});
        }
    });
    QObject::connect(player, &QMediaPlayer::errorOccurred, player, [finish]() { finish({}); });
    QObject::connect(sink, &QVideoSink::videoFrameChanged, player, [st, finish](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
        QImage img = frame.toImage();
        if (img.isNull())
            return;
        st->last = img;
        // Frames from before the seek landed: wait for the right one.
        if (st->target > 0 && frame.startTime() >= 0 && frame.startTime() / 1000 < st->target - 1500)
            return;
        finish(img);
    });
    QTimer::singleShot(6000, player, [finish]() { finish({}); });
    player->setSource(QUrl::fromLocalFile(path));
}
#endif

} // namespace

namespace ThumbMakers {

bool canDoVideo()
{
#ifdef DIRDUO_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

bool canDoDocuments()
{
    return PdfDoc::available();
}

void install()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    if (PdfDoc::available()) {
        Thumbnails::setDocumentMaker([](const QString &path, int size) -> QImage {
            PdfDoc doc;
            if (!doc.load(path) || doc.pageCount() < 1)
                return {};
            const QSizeF pts = doc.pagePoints(0);
            if (pts.isEmpty())
                return {};
            const QSize px = pts.toSize().scaled(size, size, Qt::KeepAspectRatio);
            return doc.render(0, px);
        });
    }
#ifdef DIRDUO_HAVE_MULTIMEDIA
    Thumbnails::setVideoMaker(grabVideoFrame);
#endif
    syncOptions();
    QObject::connect(Prefs::instance(), &Prefs::changed, ThumbnailCache::instance(), []() { syncOptions(); });
}

} // namespace ThumbMakers
