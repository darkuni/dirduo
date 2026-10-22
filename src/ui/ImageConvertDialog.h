#pragma once

#include "core/ImageConvert.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTreeWidget;

// Converts the selected pictures: format, size, turning and flipping, with a
// row per picture showing what it becomes before anything is written. The
// work runs off the main thread; Stop finishes the picture in hand.
class ImageConvertDialog : public QDialog
{
    Q_OBJECT
public:
    // destHint: the destination display's folder, offered as somewhere to put them.
    ImageConvertDialog(const QStringList &files, const ImageOptions &options, const QString &destHint,
                       QWidget *parent = nullptr);
    ~ImageConvertDialog() override;

    ImageOptions options() const;
    void setOptions(const ImageOptions &o);
    QList<ImagePlan> plans() const { return m_plans; }
    QTreeWidget *tree() const { return m_tree; }
    QString summary() const;

    void startConvert();
    bool isConverting() const { return m_busy; }
    QList<ImageResult> results() const { return m_results; }

    // The last options used, kept in linopus.ini.
    static ImageOptions savedOptions();
    static void saveOptions(const ImageOptions &o);

Q_SIGNALS:
    void converted(const QList<ImageResult> &results);

private:
    void schedulePlan();
    void refreshPlan();
    void updateControls();

    QStringList m_files;
    QString m_destHint;
    QList<ImagePlan> m_plans;
    QList<ImageResult> m_results;
    std::shared_ptr<ImageProgress> m_progress;
    QFutureWatcher<QList<ImageResult>> m_watcher;
    QTimer m_planTimer;
    QTimer m_tick;
    bool m_busy = false;
    bool m_loading = false;

    QComboBox *m_format;
    QSpinBox *m_quality;
    QLabel *m_qualityLabel;
    QComboBox *m_resize;
    QSpinBox *m_percent;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QCheckBox *m_noEnlarge;
    QComboBox *m_rotate;
    QCheckBox *m_flipH;
    QCheckBox *m_flipV;
    QRadioButton *m_sameFolder;
    QRadioButton *m_otherFolder;
    QLineEdit *m_folder;
    QPushButton *m_browse;
    QPushButton *m_useDest;
    QComboBox *m_whenExists;
    QTreeWidget *m_tree;
    QProgressBar *m_bar;
    QLabel *m_status;
    QPushButton *m_convert;
};
