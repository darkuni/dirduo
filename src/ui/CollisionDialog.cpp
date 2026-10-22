#include "ui/CollisionDialog.h"

#include "core/FileOps.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMimeDatabase>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QPixmap previewFor(const QString &path, bool isDir)
{
    constexpr int kSize = 96;
    if (!isDir) {
        QImageReader reader(path);
        if (reader.canRead()) {
            QSize s = reader.size();
            if (s.isValid()) {
                s.scale(kSize, kSize, Qt::KeepAspectRatio);
                reader.setScaledSize(s);
            }
            const QImage img = reader.read();
            if (!img.isNull())
                return QPixmap::fromImage(img);
        }
    }
    QIcon icon;
    if (isDir) {
        icon = QIcon::fromTheme(QStringLiteral("folder"));
    } else {
        const QMimeType t = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension);
        icon = QIcon::fromTheme(t.iconName(), QIcon::fromTheme(t.genericIconName()));
    }
    return icon.pixmap(64, 64);
}

} // namespace

CollisionDialog::CollisionDialog(const CollisionRequest &request, int remainingHint, QWidget *parent)
    : QDialog(parent)
    , m_request(request)
{
    setWindowTitle(QStringLiteral("Item already exists"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    const QString name = QFileInfo(request.target).fileName();
    auto *intro = new QLabel(QStringLiteral("<b>%1</b> already exists in %2.")
                                 .arg(name.toHtmlEscaped(), QFileInfo(request.target).absolutePath().toHtmlEscaped()),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const QDateTime srcTime = QFileInfo(request.source).lastModified();
    const QDateTime dstTime = QFileInfo(request.target).lastModified();
    auto *sides = new QHBoxLayout;
    sides->addWidget(sideWidget(QStringLiteral("Incoming"), request.source, true, srcTime > dstTime));
    sides->addWidget(sideWidget(QStringLiteral("Already there"), request.target, true, dstTime > srcTime));
    layout->addLayout(sides);

    auto *renameRow = new QHBoxLayout;
    renameRow->addWidget(new QLabel(QStringLiteral("Rename incoming to:"), this));
    m_newName = new QLineEdit(request.suggestedName, this);
    renameRow->addWidget(m_newName, 1);
    layout->addLayout(renameRow);

    m_applyAll = new QCheckBox(remainingHint > 1 ? QStringLiteral("Do this for every conflict in this job")
                                                 : QStringLiteral("Do this for the rest of this job"),
                               this);
    layout->addWidget(m_applyAll);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *replace = buttons->addButton(QStringLiteral("Replace"), QDialogButtonBox::AcceptRole);
    QPushButton *newer = buttons->addButton(QStringLiteral("Replace if Newer"), QDialogButtonBox::AcceptRole);
    QPushButton *rename = buttons->addButton(QStringLiteral("Rename"), QDialogButtonBox::AcceptRole);
    QPushButton *skip = buttons->addButton(QStringLiteral("Skip"), QDialogButtonBox::RejectRole);
    QPushButton *cancel = buttons->addButton(QStringLiteral("Cancel Job"), QDialogButtonBox::DestructiveRole);
    if (request.targetIsDir && !request.sourceIsDir) {
        replace->setEnabled(false);
        newer->setEnabled(false);
        replace->setToolTip(QStringLiteral("A folder is in the way; it will not be replaced by a file."));
    }
    skip->setDefault(true);
    layout->addWidget(buttons);

    connect(replace, &QPushButton::clicked, this, [this]() { finish(CollisionAction::Replace); });
    connect(newer, &QPushButton::clicked, this, [this]() { finish(CollisionAction::ReplaceIfNewer); });
    connect(rename, &QPushButton::clicked, this, [this]() { finish(CollisionAction::Rename); });
    connect(skip, &QPushButton::clicked, this, [this]() { finish(CollisionAction::Skip); });
    connect(cancel, &QPushButton::clicked, this, [this]() { finish(CollisionAction::CancelJob); });
}

QWidget *CollisionDialog::sideWidget(const QString &heading, const QString &path, bool highlightNewer, bool isNewer)
{
    const QFileInfo fi(path);
    auto *box = new QGroupBox(heading, this);
    auto *v = new QVBoxLayout(box);
    auto *pic = new QLabel(box);
    pic->setAlignment(Qt::AlignCenter);
    pic->setMinimumHeight(100);
    pic->setPixmap(previewFor(path, fi.isDir()));
    v->addWidget(pic);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Size:"), new QLabel(fi.isDir() ? QStringLiteral("Folder") : FileOps::formatSize(fi.size()), box));
    QString when = QLocale().toString(fi.lastModified(), QLocale::ShortFormat);
    if (highlightNewer && isNewer)
        when += QStringLiteral("  (newer)");
    auto *date = new QLabel(when, box);
    if (highlightNewer && isNewer) {
        QFont f = date->font();
        f.setBold(true);
        date->setFont(f);
    }
    form->addRow(QStringLiteral("Modified:"), date);
    auto *where = new QLabel(fi.absolutePath(), box);
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("In:"), where);
    v->addLayout(form);
    return box;
}

void CollisionDialog::finish(CollisionAction action)
{
    m_decision.action = action;
    m_decision.newName = m_newName->text().trimmed();
    m_decision.applyToAll = m_applyAll->isChecked();
    accept();
}
