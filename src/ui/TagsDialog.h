#pragma once

#include "core/FileTags.h"

#include <QDialog>
#include <QHash>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QToolButton;

// Label, rating, tags and description for one item or many. With several,
// what they share shows and a field left alone stays as each one has it.
class TagsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TagsDialog(const QStringList &paths, QWidget *parent = nullptr);

    // Each item's new data (only fields that were changed differ).
    QList<QPair<QString, FileTagData>> changes() const;

    QComboBox *labelBox() const { return m_label; }
    void setRating(int stars);
    int rating() const { return m_rating; }
    QListWidget *tagList() const { return m_tags; }
    QLineEdit *newTagEdit() const { return m_newTag; }
    QPlainTextEdit *commentEdit() const { return m_comment; }

private:
    void addTag(const QString &tag, Qt::CheckState state);

    QStringList m_paths;
    QList<FileTagData> m_before;
    QComboBox *m_label;
    QList<QToolButton *> m_stars;
    int m_rating = 0;
    bool m_ratingTouched = false;
    bool m_ratingMixed = false;
    QListWidget *m_tags;
    QLineEdit *m_newTag;
    QPlainTextEdit *m_comment;
    bool m_commentTouched = false;
    QHash<QString, Qt::CheckState> m_tagStart;
};
