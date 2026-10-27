#include "ui/ViewerWindow.h"

#include "core/Apps.h"
#include "core/Collections.h"
#include "core/FileOps.h"
#include "core/HostRun.h"
#include "core/ImageConvert.h"
#include "core/Location.h"
#include "core/Metadata.h"
#include "core/Paths.h"
#include "core/Prefs.h"
#include "ui/ListerWindow.h"
#include "ui/MediaPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace {
QList<ViewerWindow *> g_windows;
QPointer<ViewerWindow> g_recent;
constexpr double kZoomStep = 1.25;
} // namespace

// ---------------------------------------------------------------- picture

// One picture, as the standalone viewer shows it: fitted, grown or at a set
// zoom; turned, flipped and cropped (all on a working copy, the file is left
// alone); dragged around when bigger than the window; with a selection made
// by Shift+drag, an information overlay and a star when marked.
class PictureView : public QWidget
{
public:
    enum class Mode { Fit, Grow, Fixed };

    explicit PictureView(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(80, 80);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(false);
    }

    std::function<void()> onFullScreen;
    std::function<void(int)> onStep;
    std::function<void()> onChanged; // zoom, turn, selection

    void setImage(const QImage &img, int resetZoom)
    {
        m_original = img;
        m_shown = img;
        m_rotation = 0;
        m_selection = {};
        m_offset = {};
        m_mode = resetZoom == 1 ? Mode::Grow : resetZoom == 2 ? Mode::Fixed : Mode::Fit;
        m_zoom = 1.0;
        update();
    }
    void clear()
    {
        m_original = m_shown = QImage();
        update();
    }
    QImage shown() const { return m_shown; }
    Mode mode() const { return m_mode; }
    int rotation() const { return m_rotation; }
    QRect selection() const { return m_selection; }
    void setSelection(const QRect &r)
    {
        m_selection = r.intersected(m_shown.rect());
        update();
        changed();
    }
    void setBackground(int mode)
    {
        m_background = mode;
        update();
    }
    void setOverlay(const QString &text)
    {
        m_overlay = text;
        update();
    }
    void setOverlayShown(bool on)
    {
        m_overlayOn = on;
        update();
    }
    bool overlayShown() const { return m_overlayOn; }
    void setMarked(bool on)
    {
        m_marked = on;
        update();
    }
    void setFullScreenLook(bool on)
    {
        m_fullScreen = on;
        update();
    }

    double scale() const
    {
        if (m_shown.isNull())
            return 1.0;
        const double fit = qMin(width() / double(qMax(1, m_shown.width())), height() / double(qMax(1, m_shown.height())));
        switch (m_mode) {
        case Mode::Fit:
            return qMin(1.0, fit);
        case Mode::Grow:
            return fit;
        case Mode::Fixed:
            break;
        }
        return m_zoom;
    }
    void setZoom(double z)
    {
        // Keep the middle of the window on the same spot of the picture.
        const double before = scale();
        m_zoom = qBound(0.02, z, 32.0);
        m_mode = Mode::Fixed;
        if (before > 0)
            m_offset *= m_zoom / before;
        clampOffset();
        update();
        changed();
    }
    void setMode(Mode m)
    {
        m_mode = m;
        m_offset = {};
        update();
        changed();
    }
    void rotate(int degrees)
    {
        if (m_shown.isNull())
            return;
        QTransform t;
        t.rotate(degrees);
        m_shown = m_shown.transformed(t);
        m_rotation = ((m_rotation + degrees) % 360 + 360) % 360;
        m_selection = {};
        m_offset = {};
        update();
        changed();
    }
    void rotateTo(int degrees)
    {
        restore();
        if (degrees % 360)
            rotate(degrees);
    }
    void flip(bool horizontal)
    {
        if (m_shown.isNull())
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        m_shown = m_shown.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
        m_shown = m_shown.mirrored(horizontal, !horizontal);
#endif
        m_selection = {};
        update();
        changed();
    }
    bool crop()
    {
        if (m_selection.width() < 2 || m_selection.height() < 2)
            return false;
        m_shown = m_shown.copy(m_selection);
        m_selection = {};
        m_offset = {};
        update();
        changed();
        return true;
    }
    void restore()
    {
        m_shown = m_original;
        m_rotation = 0;
        m_selection = {};
        m_offset = {};
        update();
        changed();
    }
    // Scrolls a picture bigger than the window; false when there is nowhere to go.
    bool scrollBy(int dx, int dy)
    {
        const QPointF before = m_offset;
        m_offset += QPointF(dx, dy);
        clampOffset();
        update();
        return m_offset != before;
    }
    bool overflows() const
    {
        const QSizeF s = QSizeF(m_shown.size()) * scale();
        return s.width() > width() + 1 || s.height() > height() + 1;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintBackground(p, rect(), m_background);
        if (m_shown.isNull())
            return;
        const QRectF r = target();
        if (m_shown.hasAlphaChannel() && m_background != 4)
            paintBackground(p, r.toAlignedRect(), 4); // a checkerboard behind see-through pictures
        p.setRenderHint(QPainter::SmoothPixmapTransform, scale() < 4.0); // big zooms show the pixels
        p.drawImage(r, m_shown);
        if (!m_selection.isEmpty()) {
            const double s = scale();
            const QRectF sel(r.topLeft() + QPointF(m_selection.x() * s, m_selection.y() * s),
                             QSizeF(m_selection.width() * s, m_selection.height() * s));
            QPainterPath outside;
            outside.addRect(r);
            outside.addRect(sel);
            p.fillPath(outside, QColor(0, 0, 0, 90));
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawRect(sel);
            const QString size = QStringLiteral("%1 x %2").arg(m_selection.width()).arg(m_selection.height());
            drawBadge(p, sel.bottomRight().toPoint() + QPoint(-6, 6), size, Qt::AlignRight | Qt::AlignTop);
        }
        if (m_marked) {
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * 2.2);
            p.setFont(f);
            p.setPen(QColor(0, 0, 0, 160));
            p.drawText(QPoint(15, 15 + QFontMetrics(f).ascent()), QStringLiteral("★"));
            p.setPen(QColor(0xff, 0xc8, 0x20));
            p.drawText(QPoint(13, 13 + QFontMetrics(f).ascent()), QStringLiteral("★"));
            p.setFont(font());
        }
        if (m_overlayOn && !m_overlay.isEmpty())
            drawBadge(p, QPoint(width() - 12, height() - 12), m_overlay, Qt::AlignRight | Qt::AlignBottom);
    }
    void resizeEvent(QResizeEvent *) override
    {
        clampOffset();
        changed();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_dragFrom = e->position().toPoint();
        m_selecting = false;
        if (e->button() == Qt::MiddleButton) {
            if (onFullScreen)
                onFullScreen();
        } else if (e->button() == Qt::BackButton) {
            if (onStep)
                onStep(-1);
        } else if (e->button() == Qt::ForwardButton) {
            if (onStep)
                onStep(1);
        } else if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier) && !m_shown.isNull()) {
            m_selecting = true;
            m_selectFrom = toImage(e->position());
            m_selection = {};
            update();
        } else if (e->button() == Qt::LeftButton && !m_selection.isEmpty()) {
            m_selection = {};
            update();
            changed();
        }
        if (e->button() == Qt::LeftButton && !m_selecting && overflows())
            setCursor(Qt::ClosedHandCursor);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton))
            return;
        if (m_selecting) {
            const QPoint now = toImage(e->position());
            m_selection = QRect(m_selectFrom, now).normalized().intersected(m_shown.rect());
            update();
            return;
        }
        const QPoint now = e->position().toPoint();
        m_offset += now - m_dragFrom; // drag to move a big picture around
        m_dragFrom = now;
        clampOffset();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        unsetCursor();
        if (m_selecting) {
            m_selecting = false;
            if (m_selection.width() < 2 || m_selection.height() < 2)
                m_selection = {};
            update();
            changed();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onFullScreen)
            onFullScreen();
    }

private:
    void changed()
    {
        if (onChanged)
            onChanged();
    }
    QRectF target() const
    {
        const QSizeF s = QSizeF(m_shown.size()) * scale();
        return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2) + m_offset, s);
    }
    QPoint toImage(const QPointF &widgetPos) const
    {
        const QRectF r = target();
        const double s = scale();
        return QPoint(qRound((widgetPos.x() - r.left()) / s), qRound((widgetPos.y() - r.top()) / s));
    }
    void clampOffset()
    {
        const QSizeF s = QSizeF(m_shown.size()) * scale();
        const double mx = qMax(0.0, (s.width() - width()) / 2);
        const double my = qMax(0.0, (s.height() - height()) / 2);
        m_offset.setX(qBound(-mx, m_offset.x(), mx));
        m_offset.setY(qBound(-my, m_offset.y(), my));
    }
    void paintBackground(QPainter &p, const QRect &r, int mode)
    {
        switch (mode) {
        case 1:
            p.fillRect(r, Qt::black);
            return;
        case 2:
            p.fillRect(r, Qt::white);
            return;
        case 3:
            p.fillRect(r, m_fullScreen ? QColor(Qt::black) : palette().color(QPalette::Base));
            return;
        case 4: {
            const int c = 10;
            p.save();
            p.setClipRect(r);
            for (int y = r.top(); y < r.bottom(); y += c)
                for (int x = r.left(); x < r.right(); x += c)
                    p.fillRect(QRect(x, y, c, c), (((x - r.left()) / c + (y - r.top()) / c) % 2) ? QColor(0xcc, 0xcc, 0xcc)
                                                                                                 : QColor(0xf4, 0xf4, 0xf4));
            p.restore();
            return;
        }
        default:
            p.fillRect(r, m_fullScreen ? QColor(Qt::black) : QColor(0x18, 0x18, 0x18));
        }
    }
    void drawBadge(QPainter &p, const QPoint &anchor, const QString &text, Qt::Alignment align)
    {
        const QFontMetrics fm(font());
        QRect box = fm.boundingRect(QRect(0, 0, 600, 400), Qt::AlignLeft | Qt::TextWordWrap, text).adjusted(-8, -6, 8, 6);
        box.moveTo(align & Qt::AlignRight ? anchor.x() - box.width() : anchor.x(),
                   align & Qt::AlignBottom ? anchor.y() - box.height() : anchor.y());
        box = box.intersected(rect().adjusted(4, 4, -4, -4)) == box ? box : box.translated(qMin(0, rect().right() - 4 - box.right()), qMin(0, rect().bottom() - 4 - box.bottom()));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(Qt::white);
        p.drawText(box.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::TextWordWrap, text);
    }

    QImage m_original;
    QImage m_shown;
    Mode m_mode = Mode::Fit;
    double m_zoom = 1.0;
    int m_rotation = 0;
    QPointF m_offset;
    QPoint m_dragFrom;
    bool m_selecting = false;
    QPoint m_selectFrom;
    QRect m_selection;
    int m_background = 0;
    QString m_overlay;
    bool m_overlayOn = false;
    bool m_marked = false;
    bool m_fullScreen = false;
};

// ---------------------------------------------------------------- window

ViewerWindow::ViewerWindow(ListerWindow *lister)
    : QMainWindow(nullptr)
    , m_lister(lister)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("viewerWindow"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("view-preview")));
    g_windows << this;
    g_recent = this;

    m_stack = new QStackedWidget(this);
    m_picture = new PictureView(m_stack);
    m_picture->setObjectName(QStringLiteral("viewer-picture"));
    m_picture->onFullScreen = [this]() { setFullScreenMode(!isFullScreen()); };
    m_picture->onStep = [this](int d) { step(d); };
    m_picture->onChanged = [this]() { updateStatus(); };
    m_media = new MediaPanel(m_stack, true);
    m_media->setObjectName(QStringLiteral("viewer-media"));
    for (QWidget *w : m_media->findChildren<QWidget *>())
        w->setFocusPolicy(Qt::NoFocus);
    m_media->setFocusPolicy(Qt::StrongFocus);
    m_text = new QPlainTextEdit(m_stack);
    m_text->setObjectName(QStringLiteral("viewer-text"));
    m_text->setReadOnly(true);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_archive = new QTreeWidget(m_stack);
    m_archive->setObjectName(QStringLiteral("viewer-archive"));
    m_archive->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size")});
    m_archive->setRootIsDecorated(false);
    m_archive->header()->setStretchLastSection(false);
    m_archive->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_message = new QLabel(m_stack);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setFocusPolicy(Qt::StrongFocus);
    for (QWidget *w : {static_cast<QWidget *>(m_picture), static_cast<QWidget *>(m_media), static_cast<QWidget *>(m_text),
                       static_cast<QWidget *>(m_archive), static_cast<QWidget *>(m_message)}) {
        m_stack->addWidget(w);
        w->installEventFilter(this);
    }
    m_text->viewport()->installEventFilter(this);
    m_archive->viewport()->installEventFilter(this);
    setCentralWidget(m_stack);

    connect(&m_watcher, &QFutureWatcher<Preview>::finished, this, [this]() {
        const Preview p = m_watcher.result();
        if (!m_pending.isEmpty() && p.path != m_pending) {
            const QString path = m_pending;
            m_watcher.setFuture(QtConcurrent::run([path]() { return Preview::load(path, 32768); }));
            return;
        }
        m_pending.clear();
        display(p);
    });
    m_slideTimer.setSingleShot(true);
    connect(&m_slideTimer, &QTimer::timeout, this, &ViewerWindow::slideTick);
    connect(m_media, &MediaPanel::ended, this, &ViewerWindow::mediaEnded);
    connect(m_media, &MediaPanel::stateChanged, this, &ViewerWindow::updateActions);
    connect(Prefs::instance(), &Prefs::changed, this, [this]() {
        applyBackground();
        updateTitle();
    });

    buildMenus();
    buildControlBar();
    m_statusLeft = new QLabel(this);
    m_statusLeft->setObjectName(QStringLiteral("viewer-status"));
    m_statusRight = new QLabel(this);
    statusBar()->addWidget(m_statusLeft, 1);
    statusBar()->addPermanentWidget(m_statusRight);
    applyBackground();

    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    if (!restoreGeometry(s.value(QStringLiteral("Viewer/geometry")).toByteArray())) {
        if (QScreen *sc = (lister ? lister->screen() : screen())) {
            resize(sc->availableSize() * 0.75);
            move(sc->availableGeometry().center() - rect().center());
        }
    }
    const bool menuBar = s.value(QStringLiteral("Viewer/menuBar"), true).toBool();
    menuWidget()->setVisible(menuBar);
    m_controls->setVisible(s.value(QStringLiteral("Viewer/controlBar"), true).toBool());
    statusBar()->setVisible(s.value(QStringLiteral("Viewer/statusBar"), true).toBool());
    m_actions.value(QStringLiteral("controlbar"))->setChecked(m_controls->isVisible());
    m_actions.value(QStringLiteral("statusbar"))->setChecked(statusBar()->isVisible());
    updateActions();
}

ViewerWindow::~ViewerWindow()
{
    g_windows.removeAll(this);
    m_watcher.waitForFinished();
}

QList<ViewerWindow *> ViewerWindow::windows()
{
    return g_windows;
}

ViewerWindow *ViewerWindow::recent()
{
    if (g_recent)
        return g_recent;
    return g_windows.isEmpty() ? nullptr : g_windows.last();
}

bool ViewerWindow::isPicture(const QString &path)
{
    return ImageConvert::readableFormats().contains(QFileInfo(path).suffix().toLower());
}

bool ViewerWindow::isMedia(const QString &path)
{
    const MediaInfo::Kind k = Metadata::kindFor(path);
    return k == MediaInfo::Kind::Audio || k == MediaInfo::Kind::Video;
}

ViewerWindow::Family ViewerWindow::familyOf(const QString &path)
{
    if (isPicture(path))
        return Family::Pictures;
    if (isMedia(path))
        return Family::Media;
    return Family::All;
}

QStringList ViewerWindow::filesIn(const QString &folder, Family family)
{
    QStringList out;
    const QDir dir(folder);
    const QStringList names = dir.entryList(QDir::Files, QDir::NoSort);
    for (const QString &n : names) {
        const QString p = dir.filePath(n);
        if (family == Family::Pictures ? isPicture(p) : family == Family::Media ? isMedia(p) : true)
            out << p;
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return FileOps::naturalCompare(a, b) < 0; });
    return out;
}

QString ViewerWindow::markNameFor(const QString &path) const
{
    QString name = Prefs::instance()->values().viewer.markCollection;
    if (name.trimmed().isEmpty())
        name = QStringLiteral("Marked Pictures - %F");
    name.replace(QLatin1String("%F"), QFileInfo(QFileInfo(path).absolutePath()).fileName());
    name.replace(QLatin1String("%D"), QDate::currentDate().toString(Qt::ISODate));
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    name = name.trimmed();
    QString why;
    if (!Collections::validName(name, &why))
        name = QStringLiteral("Marked Pictures");
    return name;
}

void ViewerWindow::open(const QString &start, const QStringList &files, Family family)
{
    g_recent = this;
    m_family = family == Family::Auto ? familyOf(start) : family;
    if (!files.isEmpty())
        m_files = files;
    else if (Prefs::instance()->values().viewer.siblings && Location::isLocal(start))
        m_files = filesIn(QFileInfo(start).absolutePath(), m_family);
    else
        m_files = {start};
    m_index = int(m_files.indexOf(start));
    if (m_index < 0) {
        m_files.prepend(start);
        m_index = 0;
    }
    m_markName = markNameFor(start);
    m_preview = Preview();
    load();
}

void ViewerWindow::setLinked(bool on)
{
    m_linked = on;
    if (QAction *a = m_actions.value(QStringLiteral("link")))
        a->setChecked(on);
    updateStatus();
}

QString ViewerWindow::textShown() const
{
    return m_text->toPlainText();
}

void ViewerWindow::step(int delta)
{
    if (m_files.isEmpty())
        return;
    int i = m_index + delta;
    if (Prefs::instance()->values().viewer.wrap)
        i = ((i % int(m_files.size())) + int(m_files.size())) % int(m_files.size());
    else
        i = qBound(0, i, int(m_files.size()) - 1);
    goTo(i);
}

void ViewerWindow::goTo(int index)
{
    if (index < 0 || index >= m_files.size())
        return;
    if (index == m_index && (m_preview.path == current() || m_pending == current()))
        return;
    m_index = index;
    load();
}

void ViewerWindow::goRandom()
{
    if (m_files.size() < 2)
        return;
    int i = m_index;
    while (i == m_index)
        i = int(QRandomGenerator::global()->bounded(int(m_files.size())));
    goTo(i);
}

void ViewerWindow::load()
{
    m_pending = current();
    updateTitle();
    if (m_watcher.isRunning())
        return; // picked up when the one being loaded is done
    const QString path = m_pending;
    m_watcher.setFuture(QtConcurrent::run([path]() { return Preview::load(path, 32768); }));
}

void ViewerWindow::display(const Preview &p)
{
    if (p.kind != Preview::Kind::Media || p.path != m_media->source())
        m_media->stop();
    m_preview = p;
    const Prefs::Values::Viewer &v = Prefs::instance()->values().viewer;
    m_picture->setMarked(isMarked(p.path));
    QWidget *show = m_message;
    if (m_hex && QFileInfo(p.path).isFile()) {
        showHex();
        show = m_text;
    } else {
        switch (p.kind) {
        case Preview::Kind::Image:
            m_picture->setImage(p.image, v.resetZoom);
            show = m_picture;
            break;
        case Preview::Kind::Text:
        case Preview::Kind::Hex:
            m_text->setPlainText(p.truncated ? p.text + QStringLiteral("\n[... the rest is not shown ...]") : p.text);
            show = m_text;
            break;
        case Preview::Kind::Archive:
            m_archive->clear();
            for (const QString &e : p.entries)
                new QTreeWidgetItem(m_archive, e.split(QLatin1Char('\t')));
            m_archive->resizeColumnToContents(1);
            show = m_archive;
            break;
        case Preview::Kind::Media:
            m_media->load(p.path, p.text);
            if (v.autoPlay || m_slideOn)
                m_media->play();
            show = m_media;
            break;
        case Preview::Kind::Folder:
            m_message->setText(p.title + QStringLiteral("\n\n") + p.text);
            break;
        case Preview::Kind::Info:
            m_message->setText(!p.error.isEmpty() ? p.error : !p.text.isEmpty() ? p.text : QStringLiteral("No preview for this kind of file."));
            break;
        case Preview::Kind::None:
            m_message->setText(QStringLiteral("%1 is not there any more.").arg(p.title));
            break;
        }
    }
    if (show != m_picture)
        m_picture->clear();
    m_stack->setCurrentWidget(show);
    if (isActiveWindow() || !m_stack->hasFocus())
        show->setFocus();
    QStringList overlay{p.title};
    if (!p.info.isEmpty())
        overlay << p.info;
    overlay << QStringLiteral("%1 of %2").arg(m_index + 1).arg(m_files.size());
    m_picture->setOverlay(overlay.join(QLatin1Char('\n')));
    if (m_slideOn) {
        if (p.kind == Preview::Kind::Media)
            m_slideTimer.stop(); // on to the next when it ends
        else
            m_slideTimer.start(int(v.slideSeconds * 1000));
    }
    updateTitle();
    updateStatus();
    updateActions();
    Q_EMIT currentChanged(p.path);
}

void ViewerWindow::showHex()
{
    QFile f(current());
    if (!f.open(QIODevice::ReadOnly)) {
        m_text->setPlainText(f.errorString());
        return;
    }
    const qint64 limit = 256 * 1024;
    QString dump = Preview::hexDump(f.read(limit));
    if (f.size() > limit)
        dump += QStringLiteral("\n[... the first %1 of %2 ...]").arg(FileOps::formatSize(limit), FileOps::formatSize(f.size()));
    m_text->setPlainText(dump);
}

void ViewerWindow::updateTitle()
{
    const QString path = current();
    const QString name = Prefs::instance()->values().viewer.fullPathTitle ? Location::display(path) : Location::fileName(path);
    setWindowTitle(m_files.size() > 1 ? QStringLiteral("%1 (%2 of %3) - Directory Duo").arg(name).arg(m_index + 1).arg(m_files.size())
                                      : QStringLiteral("%1 - Directory Duo").arg(name));
}

void ViewerWindow::updateStatus()
{
    if (!m_statusLeft)
        return;
    QStringList left;
    left << QStringLiteral("%1 of %2").arg(m_index + 1).arg(m_files.size());
    if (m_preview.kind == Preview::Kind::Image && !m_picture->shown().isNull()) {
        const QSize s = m_picture->shown().size();
        left << QStringLiteral("%1 x %2").arg(s.width()).arg(s.height());
        left << QStringLiteral("%1%").arg(qRound(m_picture->scale() * 100));
        if (m_picture->rotation())
            left << QStringLiteral("turned %1°").arg(m_picture->rotation());
        if (!m_picture->selection().isEmpty())
            left << QStringLiteral("selection %1 x %2").arg(m_picture->selection().width()).arg(m_picture->selection().height());
    }
    const QFileInfo fi(m_preview.path);
    if (fi.isFile())
        left << FileOps::formatSize(fi.size());
    m_statusLeft->setText(left.join(QStringLiteral("   ")));
    QStringList right;
    if (m_slideOn)
        right << QStringLiteral("Slideshow");
    if (m_linked)
        right << QStringLiteral("Linked");
    const int marks = int(marked().size());
    if (marks)
        right << QStringLiteral("★ %1 marked").arg(marks);
    m_statusRight->setText(right.join(QStringLiteral("   ")));
}

void ViewerWindow::updateActions()
{
    const bool picture = m_preview.kind == Preview::Kind::Image && !m_hex;
    const bool media = m_preview.kind == Preview::Kind::Media && !m_hex;
    for (const char *k : {"zoomin", "zoomout", "original", "fit", "grow", "rotl", "rotr", "rot90", "rot180", "rot270", "rot0",
                          "fliph", "flipv", "copy", "selectall", "crop", "restore", "saveas", "wallpaper", "zoommenu"}) {
        if (QAction *a = m_actions.value(QString::fromLatin1(k))) {
            a->setEnabled(picture);
        }
    }
    // The control bar keeps to what works for this file.
    if (m_controls) {
        for (auto it = m_toolCopies.cbegin(); it != m_toolCopies.cend(); ++it)
            it.value()->setVisible(!m_pictureOnly.contains(it.key()) || picture || m_preview.path.isEmpty());
        QAction *lastSep = nullptr;
        bool anySince = false;
        for (QAction *a : m_controls->actions()) {
            if (a->isSeparator()) {
                if (lastSep)
                    lastSep->setVisible(anySince);
                lastSep = a;
                anySince = false;
            } else {
                anySince = anySince || a->isVisible();
            }
        }
        if (lastSep)
            lastSep->setVisible(anySince);
    }
    for (const char *k : {"play", "back", "forward", "volup", "voldown", "mute"}) {
        if (QAction *a = m_actions.value(QString::fromLatin1(k)))
            a->setEnabled(media);
    }
    if (QAction *a = m_actions.value(QStringLiteral("play"))) {
        a->setText(m_media->playing() ? QStringLiteral("Pause") : QStringLiteral("Play"));
        a->setIcon(QIcon::fromTheme(m_media->playing() ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
    }
    if (QAction *a = m_actions.value(QStringLiteral("mute")))
        a->setChecked(m_media->isMuted());
    const bool several = m_files.size() > 1;
    for (const char *k : {"prev", "next", "first", "last", "random", "slideshow"}) {
        if (QAction *a = m_actions.value(QString::fromLatin1(k)))
            a->setEnabled(several);
    }
    if (QAction *a = m_actions.value(QStringLiteral("hex")))
        a->setChecked(m_hex);
    if (QAction *a = m_actions.value(QStringLiteral("slideshow")))
        a->setChecked(m_slideOn);
    if (QAction *a = m_actions.value(QStringLiteral("fullscreen")))
        a->setChecked(isFullScreen());
    if (QAction *a = m_actions.value(QStringLiteral("info")))
        a->setChecked(m_picture->overlayShown());
    if (QAction *a = m_actions.value(QStringLiteral("mark")))
        a->setChecked(isMarked(current()));
    const bool lister = m_lister;
    for (const char *k : {"selectfile", "link", "copyto", "moveto", "openmarked"}) {
        if (QAction *a = m_actions.value(QString::fromLatin1(k)))
            a->setEnabled(lister);
    }
}

void ViewerWindow::applyBackground()
{
    m_picture->setBackground(Prefs::instance()->values().viewer.background);
}

QAction *ViewerWindow::addAction(QMenu *menu, const QString &text, const QString &icon, const QKeySequence &key,
                                 std::function<void()> fn)
{
    QAction *a = menu->addAction(QIcon::fromTheme(icon), text);
    if (!key.isEmpty())
        a->setShortcut(key);
    a->setShortcutContext(Qt::WindowShortcut);
    connect(a, &QAction::triggered, this, [fn]() { fn(); });
    QMainWindow::addAction(a); // keeps the keys working in full screen, with the menu bar hidden
    return a;
}

void ViewerWindow::buildMenus()
{
    auto keep = [this](const char *id, QAction *a) {
        a->setObjectName(QStringLiteral("viewer-action-") + QString::fromLatin1(id));
        m_actions.insert(QString::fromLatin1(id), a);
        return a;
    };
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    keep("open", addAction(file, QStringLiteral("Open..."), QStringLiteral("document-open"), QKeySequence::Open, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open"), QFileInfo(current()).absolutePath());
        if (!path.isEmpty())
            open(path);
    }));
    keep("openwith", addAction(file, QStringLiteral("Open in Its Application"), QStringLiteral("system-run"), QKeySequence(Qt::CTRL | Qt::Key_Return),
                               [this]() { openElsewhere(); }));
    keep("saveas", addAction(file, QStringLiteral("Save As..."), QStringLiteral("document-save-as"), QKeySequence::SaveAs, [this]() { saveAs(); }));
    file->addSeparator();
    keep("copyto", addAction(file, QStringLiteral("Copy To..."), QStringLiteral("edit-copy"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                             [this]() { copyOrMoveTo(false); }));
    keep("moveto", addAction(file, QStringLiteral("Move To..."), QStringLiteral("go-jump"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X),
                             [this]() { copyOrMoveTo(true); }));
    keep("delete", addAction(file, QStringLiteral("Delete"), QStringLiteral("edit-delete"), QKeySequence(Qt::Key_Delete), [this]() { deleteCurrent(); }));
    keep("wallpaper", addAction(file, QStringLiteral("Set as Wallpaper"), QStringLiteral("preferences-desktop-wallpaper"), {}, [this]() { setWallpaper(); }));
    file->addSeparator();
    keep("selectfile", addAction(file, QStringLiteral("Select in Lister"), QStringLiteral("edit-select"), QKeySequence(Qt::CTRL | Qt::Key_L),
                                 [this]() { selectInLister(); }));
    QAction *link = keep("link", addAction(file, QStringLiteral("Follow the Lister"), QStringLiteral("link"), {}, [this]() { setLinked(!m_linked); }));
    link->setCheckable(true);
    link->setToolTip(QStringLiteral("Show the file in focus in the lister here"));
    file->addSeparator();
    keep("close", addAction(file, QStringLiteral("Close"), QStringLiteral("window-close"), QKeySequence::Close, [this]() { close(); }));

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    keep("copy", addAction(edit, QStringLiteral("Copy Picture"), QStringLiteral("edit-copy"), QKeySequence::Copy, [this]() { copyToClipboard(); }));
    keep("selectall", addAction(edit, QStringLiteral("Select All"), QStringLiteral("edit-select-all"), QKeySequence::SelectAll, [this]() {
        setSelection(QRect(QPoint(0, 0), imageSize()));
    }));
    keep("crop", addAction(edit, QStringLiteral("Crop to Selection"), QStringLiteral("transform-crop"), QKeySequence(Qt::CTRL | Qt::Key_R),
                           [this]() { cropToSelection(); }));
    keep("restore", addAction(edit, QStringLiteral("Restore"), QStringLiteral("edit-undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), [this]() { restore(); }));
    edit->addSeparator();
    QAction *mark = keep("mark", addAction(edit, QStringLiteral("Mark"), QStringLiteral("rating"), QKeySequence(Qt::Key_M), [this]() { toggleMark(); }));
    mark->setCheckable(true);
    mark->setShortcuts({QKeySequence(Qt::Key_M), QKeySequence(Qt::Key_Insert)});
    keep("markhere", addAction(edit, QStringLiteral("Move the Mark Here"), QString(), QKeySequence(Qt::SHIFT | Qt::Key_M), [this]() { moveMarkHere(); }));
    keep("prevmarked", addAction(edit, QStringLiteral("Previous Marked"), QString(), QKeySequence(Qt::CTRL | Qt::Key_Left), [this]() { stepMarked(-1); }));
    keep("nextmarked", addAction(edit, QStringLiteral("Next Marked"), QString(), QKeySequence(Qt::CTRL | Qt::Key_Right), [this]() { stepMarked(1); }));
    keep("firstmarked", addAction(edit, QStringLiteral("First Marked"), QString(), QKeySequence(Qt::CTRL | Qt::Key_Up), [this]() {
        const QStringList m = marked();
        for (int i = 0; i < m_files.size(); ++i)
            if (m.contains(m_files.at(i))) {
                m_markReturn = current();
                goTo(i);
                return;
            }
    }));
    keep("lastmarked", addAction(edit, QStringLiteral("Last Marked"), QString(), QKeySequence(Qt::CTRL | Qt::Key_Down), [this]() {
        const QStringList m = marked();
        for (int i = int(m_files.size()) - 1; i >= 0; --i)
            if (m.contains(m_files.at(i))) {
                m_markReturn = current();
                goTo(i);
                return;
            }
    }));
    keep("markreturn", addAction(edit, QStringLiteral("Back to Where I Was"), QString(), QKeySequence(Qt::CTRL | Qt::Key_Space), [this]() {
        const int i = int(m_files.indexOf(m_markReturn));
        if (i >= 0)
            goTo(i);
    }));
    keep("openmarked", addAction(edit, QStringLiteral("Show Marked in the Lister"), QStringLiteral("folder-favorites"), {}, [this]() {
        if (m_lister && !marked().isEmpty())
            m_lister->runCommand(QStringLiteral("Go \"%1\" NEWTAB").arg(Collections::location(m_markName)));
    }));

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    QAction *zin = keep("zoomin", addAction(view, QStringLiteral("Zoom In"), QStringLiteral("zoom-in"), QKeySequence(Qt::Key_Equal), [this]() { zoomIn(); }));
    zin->setShortcuts({QKeySequence(Qt::Key_Equal), QKeySequence(Qt::Key_Plus)});
    keep("zoomout", addAction(view, QStringLiteral("Zoom Out"), QStringLiteral("zoom-out"), QKeySequence(Qt::Key_Minus), [this]() { zoomOut(); }));
    QAction *orig = keep("original", addAction(view, QStringLiteral("Original Size"), QStringLiteral("zoom-original"), QKeySequence(Qt::Key_O),
                                               [this]() { setZoom(1.0); }));
    orig->setShortcuts({QKeySequence(Qt::Key_O), QKeySequence(Qt::Key_Slash), QKeySequence(Qt::KeypadModifier | Qt::Key_Asterisk)});
    keep("fit", addAction(view, QStringLiteral("Fit to Window"), QStringLiteral("zoom-fit-best"), QKeySequence(Qt::Key_F), [this]() { zoomFit(); }));
    keep("grow", addAction(view, QStringLiteral("Grow to Window"), QStringLiteral("zoom-fit-page"), QKeySequence(Qt::Key_G), [this]() { zoomGrow(); }));
    QMenu *zoom = view->addMenu(QStringLiteral("Zoom"));
    keep("zoommenu", zoom->menuAction());
    const QList<int> levels{25, 50, 100, 200, 300, 400, 600, 800};
    for (int i = 0; i < levels.size(); ++i) {
        const int pct = levels.at(i);
        addAction(zoom, QStringLiteral("%1%").arg(pct), QString(), QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), [this, pct]() { setZoom(pct / 100.0); });
    }
    view->addSeparator();
    keep("rotl", addAction(view, QStringLiteral("Turn Left"), QStringLiteral("object-rotate-left"), QKeySequence(Qt::Key_L), [this]() { rotate(-90); }));
    keep("rotr", addAction(view, QStringLiteral("Turn Right"), QStringLiteral("object-rotate-right"), QKeySequence(Qt::Key_R), [this]() { rotate(90); }));
    QMenu *turn = view->addMenu(QStringLiteral("Turn To"));
    keep("rot90", addAction(turn, QStringLiteral("90°"), QString(), QKeySequence(Qt::Key_1), [this]() { rotateTo(90); }));
    keep("rot180", addAction(turn, QStringLiteral("180°"), QString(), QKeySequence(Qt::Key_2), [this]() { rotateTo(180); }));
    keep("rot270", addAction(turn, QStringLiteral("270°"), QString(), QKeySequence(Qt::Key_3), [this]() { rotateTo(270); }));
    keep("rot0", addAction(turn, QStringLiteral("As It Was"), QString(), QKeySequence(Qt::Key_0), [this]() { rotateTo(0); }));
    keep("fliph", addAction(view, QStringLiteral("Flip Horizontally"), QStringLiteral("object-flip-horizontal"), QKeySequence(Qt::Key_H),
                            [this]() { flip(true); }));
    keep("flipv", addAction(view, QStringLiteral("Flip Vertically"), QStringLiteral("object-flip-vertical"), QKeySequence(Qt::Key_V),
                            [this]() { flip(false); }));
    view->addSeparator();
    QAction *fs = keep("fullscreen", addAction(view, QStringLiteral("Full Screen"), QStringLiteral("view-fullscreen"), QKeySequence(Qt::Key_F11),
                                               [this]() { setFullScreenMode(!isFullScreen()); }));
    fs->setCheckable(true);
    fs->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence(Qt::ALT | Qt::Key_Return), QKeySequence(Qt::ALT | Qt::Key_Enter)});
    QAction *slide = keep("slideshow", addAction(view, QStringLiteral("Slideshow"), QStringLiteral("media-playlist-play"), QKeySequence(Qt::Key_S),
                                                 [this]() { setSlideshow(!m_slideOn); }));
    slide->setCheckable(true);
    slide->setShortcuts({QKeySequence(Qt::Key_S), QKeySequence(Qt::Key_P)});
    QAction *info = keep("info", addAction(view, QStringLiteral("Information"), QStringLiteral("dialog-information"), QKeySequence(Qt::Key_F10),
                                           [this]() { setInfoShown(!infoShown()); }));
    info->setCheckable(true);
    QAction *hex = keep("hex", addAction(view, QStringLiteral("Hex View"), QStringLiteral("format-text-code"), QKeySequence(Qt::CTRL | Qt::Key_H),
                                         [this]() { setHex(!m_hex); }));
    hex->setCheckable(true);
    view->addSeparator();
    QMenu *bg = view->addMenu(QStringLiteral("Background"));
    auto *bgGroup = new QActionGroup(this);
    const QStringList bgNames{QStringLiteral("Dark"), QStringLiteral("Black"), QStringLiteral("White"), QStringLiteral("Theme"), QStringLiteral("Checkerboard")};
    for (int i = 0; i < bgNames.size(); ++i) {
        QAction *a = bg->addAction(bgNames.at(i));
        a->setCheckable(true);
        a->setChecked(Prefs::instance()->values().viewer.background == i);
        bgGroup->addAction(a);
        connect(a, &QAction::triggered, this, [i]() {
            Prefs::Values v = Prefs::instance()->values();
            v.viewer.background = i;
            Prefs::instance()->setValues(v);
        });
    }
    QAction *top = keep("ontop", addAction(view, QStringLiteral("Always on Top"), QString(), {}, [this]() {
        setWindowFlag(Qt::WindowStaysOnTopHint, m_actions.value(QStringLiteral("ontop"))->isChecked());
        show();
    }));
    top->setCheckable(true);
    QAction *cb = keep("controlbar", addAction(view, QStringLiteral("Control Bar"), QString(), {}, [this]() {
        m_controls->setVisible(m_actions.value(QStringLiteral("controlbar"))->isChecked());
    }));
    cb->setCheckable(true);
    QAction *sb = keep("statusbar", addAction(view, QStringLiteral("Status Bar"), QString(), {}, [this]() {
        statusBar()->setVisible(m_actions.value(QStringLiteral("statusbar"))->isChecked());
    }));
    sb->setCheckable(true);

    QMenu *go = menuBar()->addMenu(QStringLiteral("&Go"));
    keep("prev", addAction(go, QStringLiteral("Previous"), QStringLiteral("go-previous"), QKeySequence(Qt::Key_PageUp), [this]() { step(-1); }));
    keep("next", addAction(go, QStringLiteral("Next"), QStringLiteral("go-next"), QKeySequence(Qt::Key_PageDown), [this]() { step(1); }));
    keep("first", addAction(go, QStringLiteral("First"), QStringLiteral("go-first"), QKeySequence(Qt::Key_Home), [this]() { goTo(0); }));
    keep("last", addAction(go, QStringLiteral("Last"), QStringLiteral("go-last"), QKeySequence(Qt::Key_End), [this]() { goTo(int(m_files.size()) - 1); }));
    keep("random", addAction(go, QStringLiteral("Random"), QStringLiteral("media-playlist-shuffle"), QKeySequence(Qt::Key_X), [this]() { goRandom(); }));

    QMenu *play = menuBar()->addMenu(QStringLiteral("&Play"));
    keep("play", addAction(play, QStringLiteral("Play"), QStringLiteral("media-playback-start"), QKeySequence(Qt::Key_K), [this]() { m_media->playPause(); }));
    keep("back", addAction(play, QStringLiteral("Back 10 Seconds"), QStringLiteral("media-seek-backward"), QKeySequence(Qt::Key_J),
                           [this]() { m_media->seekBy(-10000); }));
    keep("forward", addAction(play, QStringLiteral("Forward 10 Seconds"), QStringLiteral("media-seek-forward"), QKeySequence(Qt::Key_Semicolon),
                              [this]() { m_media->seekBy(10000); }));
    keep("volup", addAction(play, QStringLiteral("Louder"), QStringLiteral("audio-volume-high"), {}, [this]() { m_media->setVolume(m_media->volume() + 10); }));
    keep("voldown", addAction(play, QStringLiteral("Quieter"), QStringLiteral("audio-volume-low"), {}, [this]() { m_media->setVolume(m_media->volume() - 10); }));
    QAction *mute = keep("mute", addAction(play, QStringLiteral("Mute"), QStringLiteral("audio-volume-muted"), {}, [this]() {
        m_media->setMuted(!m_media->isMuted());
        updateActions();
    }));
    mute->setCheckable(true);
    play->addSeparator();
    QAction *next = keep("playnext", addAction(play, QStringLiteral("Play the Next File When One Ends"), QString(), {}, [this]() {
        Prefs::Values v = Prefs::instance()->values();
        v.viewer.playNext = m_actions.value(QStringLiteral("playnext"))->isChecked();
        Prefs::instance()->setValues(v);
    }));
    next->setCheckable(true);
    next->setChecked(Prefs::instance()->values().viewer.playNext);

    QMenu *settings = menuBar()->addMenu(QStringLiteral("&Settings"));
    keep("settings", addAction(settings, QStringLiteral("Viewer Settings..."), QStringLiteral("configure"), {}, [this]() {
        if (m_lister) {
            m_lister->runCommand(QStringLiteral("Settings PAGE=viewer"));
        }
    }));
    QAction *menuToggle = keep("menubar", addAction(settings, QStringLiteral("Menu Bar"), QString(), QKeySequence(Qt::CTRL | Qt::Key_M), [this]() {
        menuWidget()->setVisible(!menuWidget()->isVisible());
    }));
    Q_UNUSED(menuToggle)
}

void ViewerWindow::buildControlBar()
{
    // Opus's control bar, in its order.
    m_controls = new QToolBar(QStringLiteral("Control Bar"), this);
    m_controls->setObjectName(QStringLiteral("viewer-controls"));
    m_controls->setMovable(false);
    m_controls->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::BottomToolBarArea, m_controls);
    const QList<QList<const char *>> groups{{"prev", "next"},
                                            {"rotl", "rotr"},
                                            {"zoomin", "zoomout", "original", "fit", "grow"},
                                            {"hex", "info", "mark"},
                                            {"slideshow", "fullscreen"},
                                            {"selectfile", "settings"}};
    m_pictureOnly = {QStringLiteral("rotl"), QStringLiteral("rotr"), QStringLiteral("zoomin"), QStringLiteral("zoomout"),
                     QStringLiteral("original"), QStringLiteral("fit"), QStringLiteral("grow")};
    for (const auto &g : groups) {
        if (!m_controls->actions().isEmpty())
            m_controls->addSeparator();
        for (const char *id : g) {
            QAction *a = m_actions.value(QString::fromLatin1(id));
            if (!a)
                continue;
            // A copy that follows the menu's action, so that it can be hidden on its own.
            auto *copy = new QAction(a->icon(), a->text(), m_controls);
            copy->setObjectName(a->objectName() + QStringLiteral("-button"));
            copy->setCheckable(a->isCheckable());
            auto sync = [a, copy]() {
                copy->setEnabled(a->isEnabled());
                copy->setChecked(a->isChecked());
                copy->setIcon(a->icon());
                copy->setText(a->text());
                copy->setToolTip(a->shortcut().isEmpty() ? a->text() : QStringLiteral("%1 (%2)").arg(a->text(), a->shortcut().toString(QKeySequence::NativeText)));
            };
            sync();
            connect(a, &QAction::changed, copy, sync);
            connect(copy, &QAction::triggered, a, [a]() { a->trigger(); });
            m_controls->addAction(copy);
            m_toolCopies.insert(QString::fromLatin1(id), copy);
        }
    }
    for (QWidget *w : m_controls->findChildren<QWidget *>())
        w->setFocusPolicy(Qt::NoFocus);
}

// ---------------------------------------------------------------- pictures

QSize ViewerWindow::imageSize() const
{
    return m_picture->shown().size();
}

double ViewerWindow::scale() const
{
    return m_picture->scale();
}

QString ViewerWindow::zoomMode() const
{
    switch (m_picture->mode()) {
    case PictureView::Mode::Fit:
        return QStringLiteral("fit");
    case PictureView::Mode::Grow:
        return QStringLiteral("grow");
    case PictureView::Mode::Fixed:
        break;
    }
    return QStringLiteral("fixed");
}

int ViewerWindow::rotation() const
{
    return m_picture->rotation();
}

void ViewerWindow::zoomIn()
{
    setZoom(m_picture->scale() * kZoomStep);
}

void ViewerWindow::zoomOut()
{
    setZoom(m_picture->scale() / kZoomStep);
}

void ViewerWindow::setZoom(double factor)
{
    m_picture->setZoom(factor);
}

void ViewerWindow::zoomFit()
{
    m_picture->setMode(PictureView::Mode::Fit);
}

void ViewerWindow::zoomGrow()
{
    m_picture->setMode(PictureView::Mode::Grow);
}

void ViewerWindow::rotate(int degrees)
{
    m_picture->rotate(degrees);
}

void ViewerWindow::rotateTo(int degrees)
{
    m_picture->rotateTo(degrees);
}

void ViewerWindow::flip(bool horizontal)
{
    m_picture->flip(horizontal);
}

void ViewerWindow::restore()
{
    m_picture->restore();
}

bool ViewerWindow::cropToSelection()
{
    return m_picture->crop();
}

void ViewerWindow::setSelection(const QRect &imageRect)
{
    m_picture->setSelection(imageRect);
}

QRect ViewerWindow::selection() const
{
    return m_picture->selection();
}

QImage ViewerWindow::shownImage() const
{
    return m_picture->shown();
}

void ViewerWindow::copyToClipboard()
{
    const QImage img = m_picture->shown();
    if (img.isNull())
        return;
    const QRect sel = m_picture->selection();
    QApplication::clipboard()->setImage(sel.isEmpty() ? img : img.copy(sel));
    statusBar()->showMessage(sel.isEmpty() ? QStringLiteral("Picture copied") : QStringLiteral("Selection copied"), 2500);
}

// ---------------------------------------------------------------- modes

void ViewerWindow::setSlideshow(bool on)
{
    m_slideOn = on && m_files.size() > 1;
    if (!m_slideOn) {
        m_slideTimer.stop();
    } else if (m_preview.kind == Preview::Kind::Media) {
        if (!m_media->playing())
            m_media->play(); // on to the next when it ends
    } else {
        m_slideTimer.start(int(Prefs::instance()->values().viewer.slideSeconds * 1000));
    }
    updateStatus();
    updateActions();
}

void ViewerWindow::slideTick()
{
    if (!m_slideOn)
        return;
    if (isLoading()) {
        m_slideTimer.start(200);
        return;
    }
    advance();
}

void ViewerWindow::advance()
{
    const Prefs::Values::Viewer &v = Prefs::instance()->values().viewer;
    if (m_slideOn && v.slideRandom) {
        goRandom();
        return;
    }
    if (m_index + 1 < m_files.size()) {
        step(1);
    } else if (v.wrap || m_slideOn) {
        // A slideshow goes round; the player only when wrapping is on.
        if (v.wrap || m_preview.kind != Preview::Kind::Media)
            goTo(0);
        else
            setSlideshow(false);
    } else {
        setSlideshow(false);
    }
}

void ViewerWindow::mediaEnded()
{
    if (m_slideOn || Prefs::instance()->values().viewer.playNext) {
        if (m_index + 1 >= m_files.size() && !Prefs::instance()->values().viewer.wrap && !m_slideOn)
            return;
        advance();
    }
}

void ViewerWindow::setFullScreenMode(bool on)
{
    if (on == isFullScreen())
        return;
    if (on) {
        m_menuWasVisible = menuWidget()->isVisible();
        m_controlsWereVisible = m_controls->isVisible();
        m_statusWasVisible = statusBar()->isVisible();
        menuWidget()->hide();
        m_controls->hide();
        statusBar()->hide();
        showFullScreen();
    } else {
        showNormal();
        menuWidget()->setVisible(m_menuWasVisible);
        m_controls->setVisible(m_controlsWereVisible);
        statusBar()->setVisible(m_statusWasVisible);
    }
    m_picture->setFullScreenLook(on);
    updateActions();
}

void ViewerWindow::setHex(bool on)
{
    m_hex = on;
    if (!m_preview.path.isEmpty())
        display(m_preview);
    updateActions();
}

bool ViewerWindow::infoShown() const
{
    return m_picture->overlayShown();
}

void ViewerWindow::setInfoShown(bool on)
{
    m_picture->setOverlayShown(on);
    updateActions();
}

// ---------------------------------------------------------------- marking

bool ViewerWindow::isMarked(const QString &path) const
{
    return !m_markName.isEmpty() && !path.isEmpty() && Collections::instance()->exists(m_markName)
        && Collections::instance()->items(m_markName).contains(path);
}

QStringList ViewerWindow::marked() const
{
    if (m_markName.isEmpty() || !Collections::instance()->exists(m_markName))
        return {};
    return Collections::instance()->items(m_markName);
}

void ViewerWindow::toggleMark()
{
    const QString path = current();
    if (path.isEmpty() || !Location::isLocal(path))
        return;
    if (isMarked(path)) {
        Collections::instance()->removeItems(m_markName, {path});
    } else {
        Collections::instance()->add(m_markName, {path});
        m_lastMarked = path;
    }
    m_picture->setMarked(isMarked(path));
    updateStatus();
    updateActions();
}

void ViewerWindow::moveMarkHere()
{
    // Opus's Shift+M: for picking the best of a few similar shots.
    const QString path = current();
    if (!m_lastMarked.isEmpty() && m_lastMarked != path)
        Collections::instance()->removeItems(m_markName, {m_lastMarked});
    if (!isMarked(path))
        Collections::instance()->add(m_markName, {path});
    m_lastMarked = path;
    m_picture->setMarked(true);
    updateStatus();
    updateActions();
}

void ViewerWindow::stepMarked(int direction)
{
    const QStringList m = marked();
    if (m.isEmpty())
        return;
    for (int i = m_index + direction; i >= 0 && i < m_files.size(); i += direction) {
        if (m.contains(m_files.at(i))) {
            m_markReturn = current();
            goTo(i);
            return;
        }
    }
}

// ---------------------------------------------------------------- files

bool ViewerWindow::listerCommand(const QString &line, const QStringList &files)
{
    if (!m_lister)
        return false;
    return m_lister->runCommandWithFiles(line, files);
}

void ViewerWindow::deleteCurrent()
{
    const QString path = current();
    if (path.isEmpty())
        return;
    bool ok = false;
    if (m_lister)
        ok = listerCommand(QStringLiteral("Delete"), {path});
    else
        ok = QFile::moveToTrash(path);
    if (!ok)
        return;
    m_media->stop();
    m_files.removeAt(m_index);
    if (m_files.isEmpty()) {
        close();
        return;
    }
    m_index = qMin(m_index, int(m_files.size()) - 1);
    m_preview = Preview();
    load();
}

void ViewerWindow::copyOrMoveTo(bool move)
{
    if (!m_lister)
        return;
    const QString dest = m_lister->destPath();
    const QString dir = QFileDialog::getExistingDirectory(this, move ? QStringLiteral("Move To") : QStringLiteral("Copy To"),
                                                          dest.isEmpty() || !Location::isLocal(dest) ? QFileInfo(current()).absolutePath() : dest);
    if (dir.isEmpty())
        return;
    const QString path = current();
    if (move)
        m_media->stop();
    if (!listerCommand(QStringLiteral("%1 TO=\"%2\"").arg(move ? QStringLiteral("Move") : QStringLiteral("Copy"), dir), {path}))
        return;
    if (move) {
        m_files.removeAt(m_index);
        if (m_files.isEmpty()) {
            close();
            return;
        }
        m_index = qMin(m_index, int(m_files.size()) - 1);
        m_preview = Preview();
        load();
    }
}

void ViewerWindow::saveAs()
{
    const QImage img = m_picture->shown();
    if (img.isNull())
        return;
    const QFileInfo fi(current());
    const QString suggested = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(" (edited).")
        + (ImageConvert::writableFormats().contains(fi.suffix().toLower()) ? fi.suffix().toLower() : QStringLiteral("png"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save As"), suggested);
    if (path.isEmpty())
        return;
    if (!img.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Save As"), QStringLiteral("Could not save %1.").arg(path));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    if (m_lister)
        m_lister->afterLocalChange({path});
}

void ViewerWindow::selectInLister()
{
    if (!m_lister)
        return;
    const QString path = current();
    const QString folder = QFileInfo(path).absolutePath();
    if (m_lister->sourcePath() != folder)
        m_lister->runCommand(QStringLiteral("Go \"%1\"").arg(folder));
    m_lister->afterLocalChange({path});
    m_lister->raise();
    m_lister->activateWindow();
}

void ViewerWindow::openElsewhere()
{
    const QString path = current();
    QString why;
    if (Apps::openWithDefault(path, &why))
        return;
    if (!why.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::warning(this, QStringLiteral("Open"), why.isEmpty() ? QStringLiteral("No application is set up for this file.") : why);
}

void ViewerWindow::setWallpaper()
{
    // Plasma's own tool (Plasma 5.24 and later, as on SteamOS).
    QString err;
    if (!HostRun::start(QStringLiteral("plasma-apply-wallpaperimage"), {current()}, QDir::homePath(), &err))
        QMessageBox::warning(this, QStringLiteral("Set as Wallpaper"), QStringLiteral("This needs the KDE Plasma desktop.\n%1").arg(err));
    else
        statusBar()->showMessage(QStringLiteral("Wallpaper set"), 3000);
}

// ---------------------------------------------------------------- commands

bool ViewerWindow::runViewerCommand(const QString &command, QString *error)
{
    auto fail = [error](const QString &m) {
        if (error)
            *error = m;
        return false;
    };
    QString word = command.trimmed();
    QString arg;
    const int sep = int(word.indexOf(QRegularExpression(QStringLiteral("[,= ]"))));
    if (sep > 0) {
        arg = word.mid(sep + 1).trimmed().toLower();
        word = word.left(sep);
    }
    word = word.toLower();
    auto onOff = [&arg](bool now) {
        if (arg == QLatin1String("on") || arg == QLatin1String("true") || arg == QLatin1String("1"))
            return true;
        if (arg == QLatin1String("off") || arg == QLatin1String("false") || arg == QLatin1String("0"))
            return false;
        return !now;
    };
    if (word == QLatin1String("next")) {
        step(1);
    } else if (word == QLatin1String("prev") || word == QLatin1String("previous")) {
        step(-1);
    } else if (word == QLatin1String("first")) {
        goTo(0);
    } else if (word == QLatin1String("last")) {
        goTo(int(m_files.size()) - 1);
    } else if (word == QLatin1String("nextrandom") || word == QLatin1String("random")) {
        goRandom();
    } else if (word == QLatin1String("goto")) {
        bool num = false;
        const int n = arg.toInt(&num);
        if (num)
            goTo(arg.startsWith(QLatin1Char('+')) || arg.startsWith(QLatin1Char('-')) ? m_index + n : n - 1);
        else {
            const QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(arg), QRegularExpression::CaseInsensitiveOption);
            for (int i = 0; i < m_files.size(); ++i)
                if (rx.match(Location::fileName(m_files.at(i))).hasMatch()) {
                    goTo(i);
                    break;
                }
        }
    } else if (word == QLatin1String("zoom")) {
        if (arg == QLatin1String("in") || arg.isEmpty())
            zoomIn();
        else if (arg == QLatin1String("out"))
            zoomOut();
        else if (arg == QLatin1String("fit"))
            zoomFit();
        else if (arg == QLatin1String("grow"))
            zoomGrow();
        else if (arg == QLatin1String("reset") || arg == QLatin1String("original"))
            setZoom(1.0);
        else if (arg.remove(QLatin1Char('%')).toDouble() > 0)
            setZoom(arg.toDouble() / 100.0);
        else
            return fail(QStringLiteral("zoom takes in, out, fit, grow, reset or a percentage."));
    } else if (word == QLatin1String("rotate")) {
        if (arg == QLatin1String("left"))
            rotate(-90);
        else if (arg == QLatin1String("right") || arg.isEmpty())
            rotate(90);
        else if (arg == QLatin1String("reset") || arg == QLatin1String("0"))
            rotateTo(0);
        else if (arg.toInt() % 90 == 0)
            rotateTo(arg.toInt());
        else
            return fail(QStringLiteral("rotate takes left, right, reset, 90, 180 or 270."));
    } else if (word == QLatin1String("flip")) {
        flip(!arg.startsWith(QLatin1Char('v')));
    } else if (word == QLatin1String("restore")) {
        restore();
    } else if (word == QLatin1String("crop")) {
        cropToSelection();
    } else if (word == QLatin1String("copy")) {
        copyToClipboard();
    } else if (word == QLatin1String("fullscreen")) {
        setFullScreenMode(onOff(isFullScreen()));
    } else if (word == QLatin1String("slideshow")) {
        setSlideshow(onOff(m_slideOn));
    } else if (word == QLatin1String("info")) {
        setInfoShown(onOff(infoShown()));
    } else if (word == QLatin1String("hex")) {
        setHex(onOff(m_hex));
    } else if (word == QLatin1String("mark")) {
        if (arg == QLatin1String("next"))
            stepMarked(1);
        else if (arg == QLatin1String("prev"))
            stepMarked(-1);
        else if (arg == QLatin1String("exchange"))
            moveMarkHere();
        else if (arg == QLatin1String("on") && !isMarked(current()))
            toggleMark();
        else if (arg == QLatin1String("off") && isMarked(current()))
            toggleMark();
        else if (arg.isEmpty() || arg == QLatin1String("toggle"))
            toggleMark();
    } else if (word == QLatin1String("listerlink")) {
        setLinked(onOff(m_linked));
    } else if (word == QLatin1String("selectfile")) {
        selectInLister();
    } else if (word == QLatin1String("delete")) {
        deleteCurrent();
    } else if (word == QLatin1String("play")) {
        m_media->play();
    } else if (word == QLatin1String("pause")) {
        m_media->pause();
    } else if (word == QLatin1String("playpause")) {
        m_media->playPause();
    } else if (word == QLatin1String("alwaysontop")) {
        QAction *a = m_actions.value(QStringLiteral("ontop"));
        a->setChecked(onOff(a->isChecked()));
        a->trigger();
        a->setChecked(!a->isChecked()); // trigger flipped it back
    } else if (word == QLatin1String("close")) {
        close();
    } else {
        return fail(QStringLiteral("The viewer has no command \"%1\".").arg(word));
    }
    return true;
}

// ---------------------------------------------------------------- input

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    const bool picture = m_preview.kind == Preview::Kind::Image && !m_hex;
    const bool media = m_preview.kind == Preview::Kind::Media && !m_hex;
    const int step = 60;
    switch (e->key()) {
    case Qt::Key_Space:
        if (media)
            m_media->playPause();
        else
            this->step(1);
        return;
    case Qt::Key_Backspace:
        this->step(-1);
        return;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const int dir = e->key() == Qt::Key_Left ? -1 : 1;
        if (media)
            m_media->seekBy(dir * 5000);
        else if (!(picture && m_picture->scrollBy(-dir * step, 0)))
            this->step(dir); // nothing to scroll: previous / next
        return;
    }
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int dir = e->key() == Qt::Key_Up ? -1 : 1;
        if (media)
            m_media->setVolume(m_media->volume() - dir * 5);
        else if (picture)
            m_picture->scrollBy(0, -dir * step);
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (e->modifiers() & Qt::KeypadModifier) {
            setFullScreenMode(!isFullScreen());
            return;
        }
        break;
    case Qt::Key_Escape:
        if (isFullScreen())
            setFullScreenMode(false);
        else
            close();
        return;
    default:
        break;
    }
    QMainWindow::keyPressEvent(e);
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    const int dy = e->angleDelta().y();
    if (dy == 0)
        return;
    const bool picture = m_preview.kind == Preview::Kind::Image && !m_hex;
    const bool zoom = (e->modifiers() & Qt::ControlModifier) || !Prefs::instance()->values().viewer.wheelSteps;
    if (zoom && picture) {
        setZoom(m_picture->scale() * (dy > 0 ? 1.15 : 1 / 1.15));
        return;
    }
    if (zoom)
        return;
    static int s_accum = 0; // smooth trackpads send small steps
    s_accum += dy;
    if (qAbs(s_accum) >= 120) {
        step(s_accum > 0 ? -1 : 1);
        s_accum = 0;
    }
}

bool ViewerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(event);
        const bool textual = watched == m_text || watched == m_archive;
        // Text and lists keep their own scrolling keys; the rest go to the viewer.
        const bool scrollKey = k->key() == Qt::Key_Up || k->key() == Qt::Key_Down || k->key() == Qt::Key_Left || k->key() == Qt::Key_Right;
        if (textual && scrollKey && !(k->modifiers() & Qt::ControlModifier))
            return false;
        if (k->key() == Qt::Key_Space || k->key() == Qt::Key_Backspace || k->key() == Qt::Key_Escape || scrollKey
            || ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (k->modifiers() & Qt::KeypadModifier))) {
            if (scrollKey && (k->modifiers() & Qt::ControlModifier))
                return false; // marked-picture keys: the actions
            keyPressEvent(k);
            return true;
        }
    }
    if (event->type() == QEvent::Wheel && (watched == m_picture || watched == m_media)) {
        wheelEvent(static_cast<QWheelEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick && watched == m_media && m_media->hasVideo()) {
        setFullScreenMode(!isFullScreen());
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void ViewerWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        g_recent = this;
    if (event->type() == QEvent::WindowStateChange) {
        m_picture->setFullScreenLook(isFullScreen());
        updateActions();
    }
    QMainWindow::changeEvent(event);
}

void ViewerWindow::closeEvent(QCloseEvent *event)
{
    m_slideTimer.stop();
    m_media->stop();
    if (!isFullScreen()) {
        QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
        s.setValue(QStringLiteral("Viewer/geometry"), saveGeometry());
        s.setValue(QStringLiteral("Viewer/menuBar"), menuWidget()->isVisible());
        s.setValue(QStringLiteral("Viewer/controlBar"), m_controls->isVisible());
        s.setValue(QStringLiteral("Viewer/statusBar"), statusBar()->isVisible());
    }
    // As Opus: marked pictures open in a tab when the viewer closes.
    if (m_lister && Prefs::instance()->values().viewer.openMarkedOnClose && !marked().isEmpty())
        m_lister->runCommand(QStringLiteral("Go \"%1\" NEWTAB").arg(Collections::location(m_markName)));
    QMainWindow::closeEvent(event);
}
