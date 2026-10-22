#include "ui/ImageConvertDialog.h"

#include "core/Paths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

QString sizeText(const QSize &s)
{
    return s.isValid() ? QStringLiteral("%1 × %2").arg(s.width()).arg(s.height()) : QString();
}

enum Column { ColPicture, ColSize, ColBecomes, ColNewSize };

} // namespace

ImageConvertDialog::ImageConvertDialog(const QStringList &files, const ImageOptions &o, const QString &destHint,
                                       QWidget *parent)
    : QDialog(parent)
    , m_files(files)
    , m_destHint(destHint)
{
    setWindowTitle(QStringLiteral("Convert Images"));
    auto *layout = new QVBoxLayout(this);

    auto *grid = new QHBoxLayout;

    // Format
    auto *formatBox = new QGroupBox(QStringLiteral("Format"), this);
    auto *ff = new QFormLayout(formatBox);
    m_format = new QComboBox(formatBox);
    m_format->addItem(QStringLiteral("Keep each picture's format"), QString());
    for (const QString &f : ImageConvert::writableFormats())
        m_format->addItem(ImageConvert::displayName(f), f);
    ff->addRow(QStringLiteral("Save as:"), m_format);
    m_quality = new QSpinBox(formatBox);
    m_quality->setRange(1, 100);
    m_quality->setSuffix(QStringLiteral("%"));
    m_qualityLabel = new QLabel(QStringLiteral("Quality:"), formatBox);
    ff->addRow(m_qualityLabel, m_quality);
    m_rotate = new QComboBox(formatBox);
    m_rotate->addItem(QStringLiteral("Leave as is"), 0);
    m_rotate->addItem(QStringLiteral("90° clockwise"), 90);
    m_rotate->addItem(QStringLiteral("180°"), 180);
    m_rotate->addItem(QStringLiteral("90° anticlockwise"), 270);
    ff->addRow(QStringLiteral("Turn:"), m_rotate);
    auto *flips = new QHBoxLayout;
    m_flipH = new QCheckBox(QStringLiteral("Left to right"), formatBox);
    m_flipV = new QCheckBox(QStringLiteral("Upside down"), formatBox);
    flips->addWidget(m_flipH);
    flips->addWidget(m_flipV);
    flips->addStretch(1);
    ff->addRow(QStringLiteral("Flip:"), flips);
    grid->addWidget(formatBox, 1);

    // Size
    auto *sizeBox = new QGroupBox(QStringLiteral("Size"), this);
    auto *sf = new QFormLayout(sizeBox);
    m_resize = new QComboBox(sizeBox);
    m_resize->addItem(QStringLiteral("Keep the size"), int(ImageOptions::Resize::None));
    m_resize->addItem(QStringLiteral("Percentage"), int(ImageOptions::Resize::Percent));
    m_resize->addItem(QStringLiteral("Fit inside"), int(ImageOptions::Resize::Fit));
    m_resize->addItem(QStringLiteral("Width (height follows)"), int(ImageOptions::Resize::Width));
    m_resize->addItem(QStringLiteral("Height (width follows)"), int(ImageOptions::Resize::Height));
    m_resize->addItem(QStringLiteral("Exactly (may stretch)"), int(ImageOptions::Resize::Exact));
    sf->addRow(QStringLiteral("Resize:"), m_resize);
    m_percent = new QSpinBox(sizeBox);
    m_percent->setRange(1, 1000);
    m_percent->setSuffix(QStringLiteral("%"));
    sf->addRow(QStringLiteral("Percentage:"), m_percent);
    auto *wh = new QHBoxLayout;
    m_width = new QSpinBox(sizeBox);
    m_height = new QSpinBox(sizeBox);
    for (QSpinBox *s : {m_width, m_height}) {
        s->setRange(1, 65535);
        s->setSuffix(QStringLiteral(" px"));
    }
    wh->addWidget(m_width);
    wh->addWidget(new QLabel(QStringLiteral("×"), sizeBox));
    wh->addWidget(m_height);
    wh->addStretch(1);
    sf->addRow(QStringLiteral("Width × height:"), wh);
    m_noEnlarge = new QCheckBox(QStringLiteral("Never make a picture bigger"), sizeBox);
    sf->addRow(QString(), m_noEnlarge);
    grid->addWidget(sizeBox, 1);
    layout->addLayout(grid);

    // Where
    auto *whereBox = new QGroupBox(QStringLiteral("Save to"), this);
    auto *wl = new QGridLayout(whereBox);
    m_sameFolder = new QRadioButton(QStringLiteral("The folder each picture is in"), whereBox);
    m_otherFolder = new QRadioButton(QStringLiteral("This folder:"), whereBox);
    m_folder = new QLineEdit(whereBox);
    m_browse = new QPushButton(QStringLiteral("Browse..."), whereBox);
    m_useDest = new QPushButton(QStringLiteral("Destination"), whereBox);
    m_useDest->setToolTip(destHint.isEmpty() ? QStringLiteral("There is no destination display open")
                                             : QStringLiteral("Use the destination display's folder: %1").arg(destHint));
    m_useDest->setEnabled(!destHint.isEmpty());
    m_whenExists = new QComboBox(whereBox);
    m_whenExists->addItem(QStringLiteral("Keep both (add a number)"), int(ImageOptions::WhenExists::Rename));
    m_whenExists->addItem(QStringLiteral("Replace it"), int(ImageOptions::WhenExists::Replace));
    m_whenExists->addItem(QStringLiteral("Skip that picture"), int(ImageOptions::WhenExists::Skip));
    wl->addWidget(m_sameFolder, 0, 0, 1, 4);
    wl->addWidget(m_otherFolder, 1, 0);
    wl->addWidget(m_folder, 1, 1);
    wl->addWidget(m_browse, 1, 2);
    wl->addWidget(m_useDest, 1, 3);
    wl->addWidget(new QLabel(QStringLiteral("When a file of that name is there:"), whereBox), 2, 0, 1, 2);
    wl->addWidget(m_whenExists, 2, 2, 1, 2);
    wl->setColumnStretch(1, 1);
    layout->addWidget(whereBox);

    m_tree = new QTreeWidget(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setHeaderLabels({QStringLiteral("Picture"), QStringLiteral("Size"), QStringLiteral("Becomes"), QStringLiteral("New size")});
    m_tree->header()->setSectionResizeMode(ColPicture, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColSize, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColBecomes, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColNewSize, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_tree, 1);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, qMax(1, int(files.size())));
    m_bar->setVisible(false);
    layout->addWidget(m_bar);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_convert = buttons->addButton(QStringLiteral("Convert"), QDialogButtonBox::ActionRole);
    m_convert->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    m_convert->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    setOptions(o);

    // Any change re-plans after a moment, so typing a width does not re-read
    // every header on each key.
    m_planTimer.setSingleShot(true);
    m_planTimer.setInterval(150);
    connect(&m_planTimer, &QTimer::timeout, this, &ImageConvertDialog::refreshPlan);
    auto changed = [this]() {
        if (m_loading)
            return;
        updateControls();
        schedulePlan();
    };
    connect(m_format, &QComboBox::currentIndexChanged, this, changed);
    connect(m_quality, &QSpinBox::valueChanged, this, changed);
    connect(m_resize, &QComboBox::currentIndexChanged, this, changed);
    connect(m_percent, &QSpinBox::valueChanged, this, changed);
    connect(m_width, &QSpinBox::valueChanged, this, changed);
    connect(m_height, &QSpinBox::valueChanged, this, changed);
    connect(m_noEnlarge, &QCheckBox::toggled, this, changed);
    connect(m_rotate, &QComboBox::currentIndexChanged, this, changed);
    connect(m_flipH, &QCheckBox::toggled, this, changed);
    connect(m_flipV, &QCheckBox::toggled, this, changed);
    connect(m_sameFolder, &QRadioButton::toggled, this, changed);
    connect(m_folder, &QLineEdit::textChanged, this, [this, changed]() {
        if (!m_loading && !m_folder->text().isEmpty())
            m_otherFolder->setChecked(true);
        changed();
    });
    connect(m_whenExists, &QComboBox::currentIndexChanged, this, changed);
    connect(m_browse, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Save Converted Pictures In"), m_folder->text());
        if (!d.isEmpty())
            m_folder->setText(d);
    });
    connect(m_useDest, &QPushButton::clicked, this, [this]() { m_folder->setText(m_destHint); });
    connect(m_convert, &QPushButton::clicked, this, [this]() {
        if (m_busy && m_progress)
            m_progress->cancel = true;
        else
            startConvert();
    });

    connect(&m_watcher, &QFutureWatcher<QList<ImageResult>>::finished, this, [this]() {
        m_tick.stop();
        m_busy = false;
        m_results = m_watcher.result();
        int ok = 0, failed = 0, skipped = 0;
        for (int i = 0; i < m_results.size() && i < m_tree->topLevelItemCount(); ++i) {
            const ImageResult &r = m_results.at(i);
            QTreeWidgetItem *item = m_tree->topLevelItem(i);
            if (!r.target.isEmpty()) {
                ++ok;
                item->setIcon(ColBecomes, QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
            } else if (r.skipped) {
                ++skipped;
            } else {
                ++failed;
                item->setIcon(ColBecomes, QIcon::fromTheme(QStringLiteral("dialog-error")));
                item->setText(ColBecomes, r.error);
                item->setToolTip(ColBecomes, r.error);
            }
        }
        m_bar->setValue(m_bar->maximum());
        QStringList parts{QStringLiteral("Converted %1.").arg(ok)};
        if (failed)
            parts << QStringLiteral("%1 failed (see the list).").arg(failed);
        if (skipped)
            parts << QStringLiteral("%1 skipped.").arg(skipped);
        m_status->setText(parts.join(QLatin1Char(' ')));
        m_convert->setText(QStringLiteral("Convert Again"));
        for (QWidget *w : findChildren<QGroupBox *>())
            w->setEnabled(true);
        Q_EMIT converted(m_results);
    });
    m_tick.setInterval(150);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        if (!m_progress)
            return;
        m_bar->setValue(m_progress->done.load());
        m_status->setText(QStringLiteral("Converting %1 of %2...").arg(m_progress->done.load() + 1).arg(m_plans.size()));
    });

    resize(820, 600);
    updateControls();
    refreshPlan();
}

ImageConvertDialog::~ImageConvertDialog()
{
    if (m_progress)
        m_progress->cancel = true;
    m_watcher.waitForFinished();
}

ImageOptions ImageConvertDialog::options() const
{
    ImageOptions o;
    o.format = m_format->currentData().toString();
    o.quality = m_quality->value();
    o.resize = ImageOptions::Resize(m_resize->currentData().toInt());
    o.percent = m_percent->value();
    o.width = m_width->value();
    o.height = m_height->value();
    o.noEnlarge = m_noEnlarge->isChecked();
    o.rotate = m_rotate->currentData().toInt();
    o.flipH = m_flipH->isChecked();
    o.flipV = m_flipV->isChecked();
    if (m_otherFolder->isChecked())
        o.destFolder = m_folder->text().trimmed();
    o.whenExists = ImageOptions::WhenExists(m_whenExists->currentData().toInt());
    return o;
}

void ImageConvertDialog::setOptions(const ImageOptions &o)
{
    m_loading = true;
    const int fi = m_format->findData(o.format);
    m_format->setCurrentIndex(fi < 0 ? 0 : fi);
    m_quality->setValue(o.quality);
    m_resize->setCurrentIndex(qMax(0, m_resize->findData(int(o.resize))));
    m_percent->setValue(o.percent);
    m_width->setValue(o.width);
    m_height->setValue(o.height);
    m_noEnlarge->setChecked(o.noEnlarge);
    m_rotate->setCurrentIndex(qMax(0, m_rotate->findData(o.rotate)));
    m_flipH->setChecked(o.flipH);
    m_flipV->setChecked(o.flipV);
    m_folder->setText(o.destFolder);
    (o.destFolder.isEmpty() ? m_sameFolder : m_otherFolder)->setChecked(true);
    m_whenExists->setCurrentIndex(qMax(0, m_whenExists->findData(int(o.whenExists))));
    m_loading = false;
    updateControls();
    schedulePlan();
}

void ImageConvertDialog::updateControls()
{
    const QString fmt = m_format->currentData().toString();
    // Keeping the format: quality matters when any of the pictures is a JPEG.
    const bool quality = fmt.isEmpty() || ImageConvert::hasQuality(fmt);
    m_quality->setEnabled(quality);
    m_qualityLabel->setEnabled(quality);
    const auto r = ImageOptions::Resize(m_resize->currentData().toInt());
    m_percent->setEnabled(r == ImageOptions::Resize::Percent);
    m_width->setEnabled(r == ImageOptions::Resize::Fit || r == ImageOptions::Resize::Width || r == ImageOptions::Resize::Exact);
    m_height->setEnabled(r == ImageOptions::Resize::Fit || r == ImageOptions::Resize::Height || r == ImageOptions::Resize::Exact);
    m_noEnlarge->setEnabled(r != ImageOptions::Resize::None);
    m_folder->setEnabled(true);
}

void ImageConvertDialog::schedulePlan()
{
    m_planTimer.start();
}

void ImageConvertDialog::refreshPlan()
{
    if (m_busy)
        return;
    m_planTimer.stop();
    const ImageOptions o = options();
    m_plans = ImageConvert::plan(m_files, o);
    m_tree->clear();
    int ready = 0;
    for (const ImagePlan &p : std::as_const(m_plans)) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(ColPicture, QFileInfo(p.source).fileName());
        item->setToolTip(ColPicture, p.source);
        item->setText(ColSize, sizeText(p.sourceSize));
        if (p.target.isEmpty()) {
            item->setText(ColBecomes, p.problem);
            item->setForeground(ColBecomes, palette().brush(QPalette::Disabled, QPalette::Text));
        } else {
            ++ready;
            const bool otherFolder = QFileInfo(p.target).absolutePath() != QFileInfo(p.source).absolutePath();
            item->setText(ColBecomes, otherFolder ? p.target : QFileInfo(p.target).fileName());
            item->setToolTip(ColBecomes, p.target);
            if (QFileInfo::exists(p.target))
                item->setIcon(ColBecomes, QIcon::fromTheme(QStringLiteral("dialog-warning")));
            item->setText(ColNewSize, sizeText(p.targetSize));
        }
    }
    m_convert->setEnabled(ready > 0);
    m_convert->setText(QStringLiteral("Convert"));
    m_bar->setVisible(false);
    m_status->setText(summary());
}

QString ImageConvertDialog::summary() const
{
    int ready = 0, replacing = 0;
    for (const ImagePlan &p : m_plans) {
        if (p.target.isEmpty())
            continue;
        ++ready;
        if (QFileInfo::exists(p.target))
            ++replacing;
    }
    const int left = int(m_plans.size()) - ready;
    QString s = ready == 1 ? QStringLiteral("1 picture to convert.") : QStringLiteral("%1 pictures to convert.").arg(ready);
    if (replacing)
        s += QStringLiteral(" %1 will replace a file that is there now.").arg(replacing);
    if (left)
        s += QStringLiteral(" %1 left out.").arg(left);
    return s;
}

void ImageConvertDialog::startConvert()
{
    if (m_busy)
        return;
    refreshPlan(); // the folder may have changed since the list was made
    const ImageOptions o = options();
    if (!o.destFolder.isEmpty() && !QFileInfo(o.destFolder).isAbsolute()) {
        m_status->setText(QStringLiteral("The folder to save in needs to be a full path."));
        return;
    }
    bool any = false;
    for (const ImagePlan &p : std::as_const(m_plans))
        any = any || !p.target.isEmpty();
    if (!any)
        return;
    saveOptions(o);
    m_progress = std::make_shared<ImageProgress>();
    auto progress = m_progress;
    const QList<ImagePlan> plans = m_plans;
    m_results.clear();
    m_busy = true;
    m_bar->setRange(0, int(plans.size()));
    m_bar->setValue(0);
    m_bar->setVisible(true);
    m_convert->setText(QStringLiteral("Stop"));
    for (QWidget *w : findChildren<QGroupBox *>())
        w->setEnabled(false);
    m_watcher.setFuture(QtConcurrent::run([plans, o, progress]() { return ImageConvert::run(plans, o, progress.get()); }));
    m_tick.start();
}

ImageOptions ImageConvertDialog::savedOptions()
{
    QSettings s(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("ImageConvert"));
    ImageOptions o;
    o.format = s.value(QStringLiteral("format"), QStringLiteral("jpg")).toString();
    if (!o.format.isEmpty() && ImageConvert::normaliseFormat(o.format).isEmpty())
        o.format = QStringLiteral("png");
    o.quality = s.value(QStringLiteral("quality"), o.quality).toInt();
    ImageOptions::resizeFromName(s.value(QStringLiteral("resize")).toString(), o.resize);
    o.percent = s.value(QStringLiteral("percent"), o.percent).toInt();
    o.width = s.value(QStringLiteral("width"), o.width).toInt();
    o.height = s.value(QStringLiteral("height"), o.height).toInt();
    o.noEnlarge = s.value(QStringLiteral("noEnlarge"), o.noEnlarge).toBool();
    ImageOptions::whenExistsFromName(s.value(QStringLiteral("whenExists")).toString(), o.whenExists);
    // Turning, flipping and the folder are for one batch; they are not kept.
    return o;
}

void ImageConvertDialog::saveOptions(const ImageOptions &o)
{
    QSettings s(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("ImageConvert"));
    s.setValue(QStringLiteral("format"), o.format);
    s.setValue(QStringLiteral("quality"), o.quality);
    s.setValue(QStringLiteral("resize"), ImageOptions::resizeName(o.resize));
    s.setValue(QStringLiteral("percent"), o.percent);
    s.setValue(QStringLiteral("width"), o.width);
    s.setValue(QStringLiteral("height"), o.height);
    s.setValue(QStringLiteral("noEnlarge"), o.noEnlarge);
    s.setValue(QStringLiteral("whenExists"), ImageOptions::whenExistsName(o.whenExists));
}
