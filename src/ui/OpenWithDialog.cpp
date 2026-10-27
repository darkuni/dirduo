#include "ui/OpenWithDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeDatabase>
#include <QPushButton>
#include <QVBoxLayout>

QIcon OpenWithDialog::iconFor(const AppInfo &app)
{
    if (app.icon.startsWith(QLatin1Char('/'))) {
        QIcon i(app.icon);
        if (i.isNull())
            i = QIcon(QStringLiteral("/run/host") + app.icon);
        return i;
    }
    return QIcon::fromTheme(app.icon, QIcon::fromTheme(QStringLiteral("application-x-executable")));
}

OpenWithDialog::OpenWithDialog(const QStringList &files, QWidget *parent)
    : QDialog(parent)
    , m_files(files)
{
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(files.value(0));
    m_mime = mt.name();
    setWindowTitle(QStringLiteral("Open With"));
    auto *layout = new QVBoxLayout(this);
    auto *what = new QLabel(files.size() == 1 ? QStringLiteral("Open <b>%1</b> (%2) with:").arg(QFileInfo(files.first()).fileName().toHtmlEscaped(),
                                                                                              mt.comment().toHtmlEscaped())
                                              : QStringLiteral("Open %1 items with:").arg(files.size()),
                            this);
    layout->addWidget(what);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search applications"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(fontMetrics().height() * 3 / 2, fontMetrics().height() * 3 / 2));
    layout->addWidget(m_list, 1);
    m_always = new QCheckBox(QStringLiteral("Always use it for %1 files (in DirDuo)").arg(mt.comment()), this);
    layout->addWidget(m_always);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(m_search, &QLineEdit::textChanged, this, &OpenWithDialog::fill);
    connect(m_list, &QListWidget::currentRowChanged, buttons->button(QDialogButtonBox::Open), [this, buttons](int row) {
        buttons->button(QDialogButtonBox::Open)->setEnabled(row >= 0 && (m_list->item(row)->flags() & Qt::ItemIsSelectable));
    });
    fill();
    resize(fontMetrics().averageCharWidth() * 60, fontMetrics().height() * 28);
}

void OpenWithDialog::fill()
{
    m_list->clear();
    const QString q = m_search->text().trimmed();
    const QList<AppInfo> suggested = Apps::forFile(m_files.value(0));
    QStringList shown;
    auto add = [&](const AppInfo &a) {
        if (shown.contains(a.id) || (!q.isEmpty() && !a.name.contains(q, Qt::CaseInsensitive)))
            return;
        shown << a.id;
        auto *it = new QListWidgetItem(iconFor(a), a.name, m_list);
        it->setData(Qt::UserRole, a.id);
        it->setToolTip(a.id);
    };
    auto heading = [&](const QString &text) {
        auto *h = new QListWidgetItem(text, m_list);
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
        h->setFlags(Qt::NoItemFlags);
    };
    if (!suggested.isEmpty()) {
        heading(QStringLiteral("For this type"));
        for (const AppInfo &a : suggested)
            add(a);
    }
    heading(QStringLiteral("All applications"));
    for (const AppInfo &a : Apps::all())
        add(a);
    for (int r = 0; r < m_list->count(); ++r) {
        if (m_list->item(r)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(r);
            break;
        }
    }
}

AppInfo OpenWithDialog::chosen() const
{
    QListWidgetItem *it = m_list->currentItem();
    return it ? Apps::find(it->data(Qt::UserRole).toString()) : AppInfo{};
}

bool OpenWithDialog::alwaysUse() const
{
    return m_always->isChecked();
}
