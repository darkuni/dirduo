#pragma once

#include <QDialog>
#include <QStringList>

class ListerWindow;
class QLineEdit;
class QPlainTextEdit;

// Type a command and run it in the lister, as a button would. Tab completes
// command and argument names, Up and Down walk the history, "help" and
// "help <command>" explain what is there.
class CommandConsole : public QDialog
{
    Q_OBJECT
public:
    explicit CommandConsole(ListerWindow *lister);

    // Runs one line as if typed; returns false when it failed.
    bool submit(const QString &line);
    QString output() const;
    QLineEdit *input() const { return m_input; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void print(const QString &text, const QString &color = {});
    void completeInput();

    ListerWindow *m_lister;
    QPlainTextEdit *m_output;
    QLineEdit *m_input;
    QStringList m_history;
    int m_historyPos = -1;
};
