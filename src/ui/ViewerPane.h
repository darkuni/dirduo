#pragma once

#include <QDockWidget>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>

class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class ImageView;

// What the viewer made of a file; built off the GUI thread.
struct Preview
{
    enum class Kind { None, Image, Text, Hex, Archive, Folder, Info, Media };
    Kind kind = Kind::None;
    QString path;
    QString title;
    QString info;      // one line: type, size, date, dimensions
    QImage image;
    QString text;
    bool truncated = false;
    QStringList entries;      // archive: "path\tsize"
    QString error;
    int pages = 0; // a PDF: how many pages (image is the first)

    // Decides what to show for a path and loads it (call from any thread).
    static Preview load(const QString &path, int maxImageSide = 2048);
    // DOS code page 437, for .nfo and .diz files from the BBS days.
    static QString fromCp437(const QByteArray &bytes);
    static QString hexDump(const QByteArray &bytes, qint64 offset = 0);
    // PDFs (when built with Qt PDF).
    static bool canShowPdf();
    static bool isPdf(const QString &path);
    // A rendered PDF page on white paper (pages are see-through where nothing is drawn).
    static QImage onPaper(const QImage &page);
};

// The viewer pane: a dock beside or below the displays that shows the file
// in focus in the source display. Images fit the pane (click for 100%),
// text and code show as text, .nfo/.diz in code page 437, other files as
// hex, zips as a list of what is inside, folders as a summary.
class ViewerPane : public QDockWidget
{
    Q_OBJECT
public:
    explicit ViewerPane(QWidget *parent = nullptr);
    ~ViewerPane() override;

    void showPath(const QString &path); // debounced, loads in the background
    void showNow(const QString &path);  // for tests: loads and shows at once
    QString path() const { return m_path; }
    Preview::Kind kind() const { return m_current.kind; }
    QString infoText() const;
    QString textShown() const;
    QSize imageSize() const { return m_current.image.size(); }
    bool isLoading() const { return m_watcher.isRunning(); }
    // Music and video play here (when built with Qt Multimedia).
    static bool canPlay();
    bool mediaLoaded() const;
    void playPause();
    bool isPlaying() const;

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void start();
    void display(const Preview &p);

    QString m_path;
    QString m_pending;
    Preview m_current;
    QTimer m_debounce;
    QFutureWatcher<Preview> m_watcher;

    QLabel *m_title;
    QLabel *m_info;
    QToolButton *m_hexToggle;
    QStackedWidget *m_stack;
    ImageView *m_image;
    QPlainTextEdit *m_text;
    QTreeWidget *m_archive;
    QLabel *m_message;
    class MediaPanel *m_media = nullptr;
    bool m_forceHex = false;
};
