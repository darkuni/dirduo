#pragma once

#include "ui/ViewerPane.h" // Preview

#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>

class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QToolBar;
class QTreeWidget;
class ListerWindow;
class MediaPanel;
class PictureView;

// The standalone viewer (Opus's, which DirDuo follows): any file in its own
// window. Pictures zoom, turn and flip; music and video play, with volume and
// going on to the next file; text, hex and what is inside archives show too.
// Next/Previous step through the folder's files of the same kind (or the files
// picked), and there are a slideshow, full screen, an information overlay,
// marking (into a collection) and a link to the lister so that the file in
// focus there shows here.
//
// Keys (as Opus's):
//   Space / Backspace, wheel, Page Down / Up  next / previous (Space plays and pauses music and video)
//   Home / End                    first / last
//   Cursor keys                   scroll a big picture (else previous / next); music and video: seek, volume
//   = + / -, Ctrl+wheel           zoom in / out
//   O, /, numpad *                original size;  Ctrl+1..8  25% to 800%
//   F / G                         fit to the window / grow to fill it
//   L / R, 1 2 3, 0               turn left / right, to 90 180 270, back
//   H / V                         flip
//   Shift+drag, Ctrl+C, Ctrl+R    select, copy (the selection or the picture), crop to it
//   F10                           information;  S  slideshow;  M / Insert  mark
//   Ctrl+Left / Right             previous / next marked;  Shift+M  move the mark here
//   Alt+Enter, F11, middle click, double-click   full screen
//   Delete                        trash the file;  Escape  leave full screen / close
class ViewerWindow : public QMainWindow
{
    Q_OBJECT
public:
    // What Next/Previous go through when only one file is given.
    enum class Family { Auto, Pictures, Media, All };

    explicit ViewerWindow(ListerWindow *lister = nullptr);
    ~ViewerWindow() override;

    // Shows start; Next/Previous go through `files` (when given), else the
    // folder's files of the same kind (Auto) or of the given kind.
    void open(const QString &start, const QStringList &files = {}, Family family = Family::Auto);

    static QList<ViewerWindow *> windows();
    // The viewer most recently opened or used, if one is open.
    static ViewerWindow *recent();
    static Family familyOf(const QString &path);
    static QStringList filesIn(const QString &folder, Family family);
    static bool isPicture(const QString &path);
    static bool isMedia(const QString &path);

    ListerWindow *lister() const { return m_lister; }
    void setLister(ListerWindow *lister) { m_lister = lister; }
    // Linked: the file in focus in the lister shows here.
    bool isLinked() const { return m_linked; }
    void setLinked(bool on);

    QString current() const { return m_files.value(m_index); }
    QStringList files() const { return m_files; }
    int index() const { return m_index; }
    int count() const { return int(m_files.size()); }
    bool isLoading() const { return m_watcher.isRunning() || !m_pending.isEmpty(); }
    Preview::Kind kind() const { return m_preview.kind; }
    QString infoText() const { return m_preview.info; }
    QString textShown() const;

    void step(int delta);
    void goTo(int index);
    void goRandom();

    // Pictures
    QSize imageSize() const;  // as shown: turned and cropped
    double scale() const;     // shown size / real size
    QString zoomMode() const; // "fit", "grow" or "fixed"
    int rotation() const;
    void zoomIn();
    void zoomOut();
    void setZoom(double factor); // 1.0 = original size
    void zoomFit();
    void zoomGrow();
    void rotate(int degrees);    // by 90, -90, 180
    void rotateTo(int degrees);  // 0, 90, 180, 270
    void flip(bool horizontal);
    void restore();              // undo the turns, flips and crop
    bool cropToSelection();
    void setSelection(const QRect &imageRect); // in the picture as shown
    QRect selection() const;
    QImage shownImage() const;
    void copyToClipboard();

    // Modes
    bool isSlideshow() const { return m_slideOn; }
    void setSlideshow(bool on);
    bool isFullScreenMode() const { return isFullScreen(); }
    void setFullScreenMode(bool on);
    bool isHex() const { return m_hex; }
    void setHex(bool on);
    bool infoShown() const;
    void setInfoShown(bool on);

    // Marking (Opus's "image marking"): marked files go into a collection.
    bool isMarked(const QString &path) const;
    void toggleMark();
    void moveMarkHere();
    void stepMarked(int direction);
    QString markCollection() const { return m_markName; }
    QStringList marked() const;

    MediaPanel *media() const { return m_media; }

    // PDFs: their pages, one at a time.
    int page() const { return m_page; }
    int pageCount() const { return m_pages; }
    bool goPage(int page, bool atBottom = false);

    // Opus's VIEWERCMD: "next", "zoom,fit", "rotate=left", "fullscreen,off"...
    bool runViewerCommand(const QString &command, QString *error = nullptr);

Q_SIGNALS:
    void currentChanged(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildMenus();
    void buildControlBar();
    void load();
    void display(const Preview &p);
    void showHex();
    void pageOrFile(int delta);
    void updateTitle();
    void updateStatus();
    void updateActions();
    void applyBackground();
    void mediaEnded();
    void slideTick();
    void advance(); // the slideshow's and the player's "next"
    QString markNameFor(const QString &path) const;
    void deleteCurrent();
    void copyOrMoveTo(bool move);
    void saveAs();
    void selectInLister();
    void openElsewhere();
    void setWallpaper();
    bool listerCommand(const QString &line, const QStringList &files);
    QAction *addAction(class QMenu *menu, const QString &text, const QString &icon, const QKeySequence &key,
                       std::function<void()> fn);

    QPointer<ListerWindow> m_lister;
    bool m_linked = false;
    QStringList m_files;
    int m_index = 0;
    Family m_family = Family::Auto;
    QString m_pending;
    Preview m_preview;
    QFutureWatcher<Preview> m_watcher;
    bool m_hex = false;
    QTimer m_slideTimer;
    bool m_slideOn = false;
    QString m_markName;
    QString m_lastMarked;
    QString m_markReturn; // where Ctrl+Space goes back to
    int m_pages = 0;
    int m_page = 0;
    QSize m_pageSize;
    QString m_pdfPath;
    std::unique_ptr<class QPdfDocument> m_pdf;

    QStackedWidget *m_stack;
    PictureView *m_picture;
    MediaPanel *m_media;
    QPlainTextEdit *m_text;
    QTreeWidget *m_archive;
    QLabel *m_message;
    QToolBar *m_controls = nullptr;
    QLabel *m_statusLeft = nullptr;
    QLabel *m_statusRight = nullptr;
    QHash<QString, QAction *> m_actions;
    QHash<QString, QAction *> m_toolCopies;
    QStringList m_pictureOnly;
    bool m_menuWasVisible = true;
    bool m_controlsWereVisible = true;
    bool m_statusWasVisible = true;
};
