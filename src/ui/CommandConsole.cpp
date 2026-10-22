#include "ui/CommandConsole.h"

#include "core/CommandCatalog.h"
#include "ui/AppContext.h"
#include "ui/ListerWindow.h"
#include "ui/Style.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QVBoxLayout>

CommandConsole::CommandConsole(ListerWindow *lister)
    : QDialog(lister)
    , m_lister(lister)
{
    setWindowTitle(QStringLiteral("Command Console"));
    setModal(false);
    auto *layout = new QVBoxLayout(this);
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setMaximumBlockCount(2000);
    layout->addWidget(m_output, 1);
    m_input = new QLineEdit(this);
    m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_input->setPlaceholderText(QStringLiteral("A command, as on a button: Go /downloads NEWTAB   (Tab completes, help lists commands)"));
    layout->addWidget(m_input);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        const QString line = m_input->text();
        m_input->clear();
        submit(line);
    });
    if (AppContext *app = AppContext::instance())
        m_history = app->settings().value(QStringLiteral("Console/history")).toStringList();
    print(QStringLiteral("Commands run in this lister, on its source and destination. Type help for the list."));
    resize(640, 360);
    m_input->setFocus();
}

void CommandConsole::print(const QString &text, const QString &color)
{
    if (color.isEmpty()) {
        m_output->appendPlainText(text);
        return;
    }
    m_output->appendHtml(QStringLiteral("<span style=\"color:%1; white-space:pre-wrap\">%2</span>")
                             .arg(color, text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
}

QString CommandConsole::output() const
{
    return m_output->toPlainText();
}

bool CommandConsole::submit(const QString &rawLine)
{
    const QString line = rawLine.trimmed();
    if (line.isEmpty())
        return true;
    const QColor accent = Style::instance()->theme().sourceAccent;
    print(QStringLiteral("> ") + line, accent.isValid() ? accent.name() : QString());
    if (m_history.isEmpty() || m_history.last() != line) {
        m_history << line;
        while (m_history.size() > 100)
            m_history.removeFirst();
        if (AppContext *app = AppContext::instance())
            app->settings().setValue(QStringLiteral("Console/history"), m_history);
    }
    m_historyPos = -1;

    const QString first = line.section(QLatin1Char(' '), 0, 0).toLower();
    if (first == QLatin1String("help") || first == QLatin1String("?")) {
        print(CommandCatalog::help(line.section(QLatin1Char(' '), 1).trimmed()));
        return true;
    }
    if (first == QLatin1String("clear") || first == QLatin1String("cls")) {
        m_output->clear();
        return true;
    }
    QString error;
    const bool ok = m_lister->runCommandReporting(line, &error);
    if (ok)
        print(QStringLiteral("Done."));
    else
        print(error.isEmpty() ? QStringLiteral("Stopped.") : error, QStringLiteral("#d04040"));
    return ok;
}

void CommandConsole::completeInput()
{
    const QStringList options = CommandCatalog::complete(m_input->text());
    if (options.isEmpty())
        return;
    if (options.size() == 1) {
        m_input->setText(options.first() + (options.first().endsWith(QLatin1Char('=')) ? QString() : QStringLiteral(" ")));
        return;
    }
    // Several: fill in what they share, and list them.
    QString common = options.first();
    for (const QString &o : options) {
        int n = 0;
        while (n < common.size() && n < o.size() && common.at(n).toLower() == o.at(n).toLower())
            ++n;
        common.truncate(n);
    }
    if (common.size() > m_input->text().size())
        m_input->setText(common);
    QStringList words;
    for (const QString &o : options)
        words << o.section(QLatin1Char(' '), -1);
    print(words.join(QStringLiteral("  ")));
}

bool CommandConsole::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab) {
            completeInput();
            return true;
        }
        if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
            if (m_history.isEmpty())
                return true;
            if (m_historyPos < 0)
                m_historyPos = int(m_history.size());
            m_historyPos += ke->key() == Qt::Key_Up ? -1 : 1;
            m_historyPos = qBound(0, m_historyPos, int(m_history.size()));
            m_input->setText(m_historyPos < m_history.size() ? m_history.at(m_historyPos) : QString());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
