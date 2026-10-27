#include "ui/ViewerPane.h"
#include "ui/MediaPanel.h"

#include "core/Archive.h"

#include "core/Metadata.h"

#include "core/FileKind.h"
#include "core/FileOps.h"
#include "core/FolderSizes.h"
#include "core/Location.h"
#include "core/Zip.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

// ---------------------------------------------------------------- image view

// Fits the picture to the pane; a click switches to actual size and back.
class ImageView : public QWidget
{
public:
    explicit ImageView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(80, 80);
        setCursor(Qt::PointingHandCursor);
    }
    void setImage(const QImage &img)
    {
        m_image = img;
        m_actual = false;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Base));
        if (m_image.isNull())
            return;
        // A checkerboard behind transparent pictures.
        // Shrink to fit; never blow small pictures up.
        QSize s = m_image.size();
        if (!m_actual && (s.width() > width() || s.height() > height()))
            s.scale(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        if (m_image.hasAlphaChannel()) {
            const int c = 8;
            for (int y = target.top(); y < target.bottom(); y += c)
                for (int x = target.left(); x < target.right(); x += c)
                    p.fillRect(QRect(x, y, c, c).intersected(target), ((x / c + y / c) % 2) ? QColor(0xcc, 0xcc, 0xcc) : QColor(0xff, 0xff, 0xff));
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_image);
    }
    void mousePressEvent(QMouseEvent *) override
    {
        m_actual = !m_actual;
        update();
    }

private:
    QImage m_image;
    bool m_actual = false;
};

// ---------------------------------------------------------------- loading

QString Preview::fromCp437(const QByteArray &bytes)
{
    // The upper half of code page 437: box drawing, blocks and accents.
    static const char16_t high[128] = {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
    };
    QString out;
    out.reserve(bytes.size());
    for (const char ch : bytes) {
        const quint8 b = quint8(ch);
        if (b >= 0x80)
            out += QChar(high[b - 0x80]);
        else if (b == 0x1A)
            break; // DOS end of file
        else
            out += QChar(b);
    }
    return out;
}

QString Preview::hexDump(const QByteArray &bytes, qint64 offset)
{
    QString out;
    for (qsizetype i = 0; i < bytes.size(); i += 16) {
        out += QStringLiteral("%1  ").arg(offset + i, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < bytes.size()) {
                const quint8 b = quint8(bytes.at(i + j));
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += (b >= 0x20 && b < 0x7f) ? QChar(b) : QLatin1Char('.');
            } else {
                out += QStringLiteral("   ");
            }
            if (j == 7)
                out += QLatin1Char(' ');
        }
        out += QLatin1Char(' ') + ascii + QLatin1Char('\n');
    }
    return out;
}

Preview Preview::load(const QString &path, int maxImageSide)
{
    Preview p;
    p.path = path;
    p.title = Location::fileName(path);
    if (!Location::isLocal(path)) {
        p.kind = Kind::Info;
        p.info = Location::display(path);
        p.error = QStringLiteral("Previews of files on servers come later.");
        return p;
    }
    const QFileInfo fi(path);
    if (!fi.exists()) {
        p.kind = Kind::None;
        return p;
    }
    const QString when = QLocale().toString(fi.lastModified(), QLocale::ShortFormat);
    if (fi.isDir()) {
        p.kind = Kind::Folder;
        int files = 0;
        int folders = 0;
        QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        while (it.hasNext()) {
            it.next();
            (it.fileInfo().isDir() ? folders : files)++;
        }
        p.info = QStringLiteral("Folder, modified %1").arg(when);
        p.text = QStringLiteral("%1 folders, %2 files").arg(folders).arg(files);
        if (FolderSizes::instance()->has(path))
            p.text += QStringLiteral("\n%1 in all").arg(FileOps::formatSize(FolderSizes::instance()->result(path).bytes));
        return p;
    }
    static const QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(fi);
    p.info = QStringLiteral("%1, %2, %3").arg(mime.comment(), FileOps::formatSize(fi.size()), when);
    const QString ext = fi.suffix().toLower();

    // Pictures
    QImageReader reader(path);
    reader.setAutoTransform(true); // photos the right way up
    if (mime.name().startsWith(QLatin1String("image/")) && reader.canRead()) {
        const QSize full = reader.size();
        if (full.isValid() && (full.width() > maxImageSide || full.height() > maxImageSide))
            reader.setScaledSize(full.scaled(maxImageSide, maxImageSide, Qt::KeepAspectRatio));
        p.image = reader.read();
        if (!p.image.isNull()) {
            p.kind = Kind::Image;
            p.info = QStringLiteral("%1, %2 x %3, %4, %5")
                         .arg(mime.comment())
                         .arg(full.isValid() ? full.width() : p.image.width())
                         .arg(full.isValid() ? full.height() : p.image.height())
                         .arg(FileOps::formatSize(fi.size()), when);
            if (Metadata::kindFor(path) == MediaInfo::Kind::Image) {
                // One more line from the camera, when it left one.
                const MediaInfo m = Metadata::read(path);
                QStringList bits;
                if (!m.camera.isEmpty())
                    bits << m.camera;
                if (m.fNumber > 0)
                    bits << QStringLiteral("f/%1").arg(m.fNumber, 0, 'g', 3);
                if (!m.exposure.isEmpty())
                    bits << m.exposure;
                if (m.iso > 0)
                    bits << QStringLiteral("ISO %1").arg(m.iso);
                if (m.taken.isValid())
                    bits << QStringLiteral("taken %1").arg(QLocale().toString(m.taken, QLocale::ShortFormat));
                if (!bits.isEmpty())
                    p.info += QLatin1Char('\n') + bits.join(QStringLiteral(", "));
            }
            return p;
        }
    }

    // Music and video: what the tags say.
    const MediaInfo::Kind media = Metadata::kindFor(path);
    if ((media == MediaInfo::Kind::Audio || media == MediaInfo::Kind::Video) && ViewerPane::canPlay()) {
        p.kind = Kind::Media;
        const MediaInfo m = Metadata::read(path);
        p.text = m.isEmpty() ? fi.fileName() : m.summary();
        return p;
    }
    if (media == MediaInfo::Kind::Audio || media == MediaInfo::Kind::Video) {
        const MediaInfo m = Metadata::read(path);
        if (!m.isEmpty()) {
            p.kind = Kind::Info;
            p.text = m.summary();
            return p;
        }
    }

    // What is inside an archive (zip, 7z, RAR, tar, ISO...).
    if (Archive::available() && Archive::isArchive(path)) {
        QList<Archive::Entry> entries;
        QString err;
        if (Archive::list(path, &entries, &err)) {
            p.kind = Kind::Archive;
            int files = 0;
            qint64 total = 0;
            for (const Archive::Entry &e : std::as_const(entries)) {
                if (e.isDir)
                    continue;
                ++files;
                total += e.size;
                p.entries << e.name + QLatin1Char('\t') + FileOps::formatSize(e.size) + (e.encrypted ? QStringLiteral(" (locked)") : QString());
            }
            p.info += QStringLiteral("\n%1 files, %2 unpacked").arg(files).arg(FileOps::formatSize(total));
            if (entries.size() >= 5000)
                p.truncated = true;
            return p;
        }
    }
    // What is inside a zip (without libarchive).
    if (ext == QLatin1String("zip") && fi.size() < 64LL * 1024 * 1024) {
        QList<Zip::Entry> entries;
        QString err;
        if (Zip::read(path, entries, &err)) {
            p.kind = Kind::Archive;
            for (const Zip::Entry &e : std::as_const(entries))
                p.entries << e.name + QLatin1Char('\t') + FileOps::formatSize(e.data.size());
            return p;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        p.kind = Kind::Info;
        p.error = f.errorString();
        return p;
    }
    const qint64 textLimit = 2 * 1024 * 1024;
    const QByteArray head = f.read(textLimit);
    p.truncated = fi.size() > head.size();
    const bool nfo = ext == QLatin1String("nfo") || ext == QLatin1String("diz") || ext == QLatin1String("ans")
        || ext == QLatin1String("asc");
    const bool binary = head.left(8192).contains('\0');
    if (nfo) {
        p.kind = Kind::Text;
        p.text = fromCp437(head);
        return p;
    }
    if (!binary) { // no NUL bytes up front: text
        p.kind = Kind::Text;
        p.text = QString::fromUtf8(head);
        return p;
    }
    p.kind = Kind::Hex;
    p.text = hexDump(head.left(64 * 1024));
    p.truncated = fi.size() > 64 * 1024;
    return p;
}

// ---------------------------------------------------------------- pane

bool ViewerPane::canPlay()
{
    return MediaPanel::available();
}

bool ViewerPane::mediaLoaded() const
{
    return m_media && m_media->loaded();
}

void ViewerPane::playPause()
{
    if (m_media && m_current.kind == Preview::Kind::Media)
        m_media->playPause();
}

bool ViewerPane::isPlaying() const
{
    return m_media && m_media->playing();
}

void ViewerPane::hideEvent(QHideEvent *event)
{
    if (m_media)
        m_media->stop(); // closing the pane stops the music
    QDockWidget::hideEvent(event);
}

ViewerPane::ViewerPane(QWidget *parent)
    : QDockWidget(QStringLiteral("Viewer"), parent)
{
    setObjectName(QStringLiteral("viewer-pane"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 4, 6, 6);
    auto *head = new QHBoxLayout;
    m_title = new QLabel(body);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_hexToggle = new QToolButton(body);
    m_hexToggle->setText(QStringLiteral("Hex"));
    m_hexToggle->setCheckable(true);
    m_hexToggle->setAutoRaise(true);
    m_hexToggle->setToolTip(QStringLiteral("Show the raw bytes"));
    head->addWidget(m_title, 1);
    head->addWidget(m_hexToggle);
    layout->addLayout(head);
    m_info = new QLabel(body);
    m_info->setWordWrap(true);
    m_info->setEnabled(false);
    layout->addWidget(m_info);

    m_stack = new QStackedWidget(body);
    m_image = new ImageView(m_stack);
    m_text = new QPlainTextEdit(m_stack);
    m_text->setReadOnly(true);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_archive = new QTreeWidget(m_stack);
    m_archive->setHeaderLabels({QStringLiteral("Inside"), QStringLiteral("Size")});
    m_archive->setRootIsDecorated(false);
    m_archive->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_message = new QLabel(m_stack);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_stack->addWidget(m_image);
    m_stack->addWidget(m_text);
    m_stack->addWidget(m_archive);
    m_stack->addWidget(m_message);
    m_media = new MediaPanel(m_stack);
    m_stack->addWidget(m_media);
    layout->addWidget(m_stack, 1);
    setWidget(body);
    setMinimumWidth(180);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120); // arrowing through a folder loads only where you stop
    connect(&m_debounce, &QTimer::timeout, this, &ViewerPane::start);
    connect(&m_watcher, &QFutureWatcher<Preview>::finished, this, [this]() {
        const Preview p = m_watcher.result();
        if (p.path == m_path)
            display(p);
        if (m_pending != m_path)
            start();
    });
    connect(m_hexToggle, &QToolButton::toggled, this, [this](bool on) {
        m_forceHex = on;
        display(m_current);
    });
    display(Preview());
}

ViewerPane::~ViewerPane()
{
    m_watcher.waitForFinished();
}

void ViewerPane::showPath(const QString &path)
{
    m_pending = path;
    if (!isVisible())
        return; // nothing to do while hidden; shown again, it catches up
    m_debounce.start();
}

void ViewerPane::start()
{
    if (m_watcher.isRunning())
        return; // the finished handler starts the next one
    m_path = m_pending;
    if (m_path.isEmpty()) {
        display(Preview());
        return;
    }
    const QString path = m_path;
    m_watcher.setFuture(QtConcurrent::run([path]() { return Preview::load(path); }));
}

void ViewerPane::showNow(const QString &path)
{
    m_watcher.waitForFinished();
    m_pending = m_path = path;
    display(Preview::load(path));
}

QString ViewerPane::infoText() const
{
    return m_info->text();
}

QString ViewerPane::textShown() const
{
    return m_text->toPlainText();
}

void ViewerPane::display(const Preview &p)
{
    if (p.kind != Preview::Kind::Media || p.path != m_current.path)
        m_media->stop();
    m_current = p;
    m_title->setText(p.title);
    m_info->setText(p.info);
    m_hexToggle->setVisible(p.kind == Preview::Kind::Text || p.kind == Preview::Kind::Image || p.kind == Preview::Kind::Hex);
    if (m_forceHex && !p.path.isEmpty() && Location::isLocal(p.path) && QFileInfo(p.path).isFile()) {
        QFile f(p.path);
        if (f.open(QIODevice::ReadOnly)) {
            m_text->setPlainText(Preview::hexDump(f.read(64 * 1024)));
            m_stack->setCurrentWidget(m_text);
            return;
        }
    }
    switch (p.kind) {
    case Preview::Kind::Image:
        m_image->setImage(p.image);
        m_stack->setCurrentWidget(m_image);
        break;
    case Preview::Kind::Text:
    case Preview::Kind::Hex:
        m_text->setPlainText(p.truncated ? p.text + QStringLiteral("\n[... the rest is not shown ...]") : p.text);
        m_stack->setCurrentWidget(m_text);
        break;
    case Preview::Kind::Archive:
        m_archive->clear();
        for (const QString &e : p.entries)
            new QTreeWidgetItem(m_archive, e.split(QLatin1Char('\t')));
        m_stack->setCurrentWidget(m_archive);
        break;
    case Preview::Kind::Media:
        if (!m_media->loaded())
            m_media->load(p.path, p.text);
        m_stack->setCurrentWidget(m_media);
        break;
    case Preview::Kind::Folder:
        m_message->setText(p.text);
        m_stack->setCurrentWidget(m_message);
        break;
    case Preview::Kind::Info:
        m_message->setText(!p.error.isEmpty() ? p.error
                           : !p.text.isEmpty() ? p.text
                                               : QStringLiteral("No preview for this kind of file."));
        m_stack->setCurrentWidget(m_message);
        break;
    case Preview::Kind::None:
        m_title->setText(QStringLiteral("Nothing selected"));
        m_info->clear();
        m_message->setText(QStringLiteral("Select a file to see it here."));
        m_stack->setCurrentWidget(m_message);
        break;
    }
}
