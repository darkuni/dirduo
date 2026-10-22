#pragma once

#include "core/Metadata.h"

#include <QDockWidget>
#include <QHash>
#include <QTimer>

class CommandEngine;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

// The Metadata pane: the tags of the selected files, editable. With several
// files selected, a field they share shows its value, one that differs shows
// "(several values)"; whatever is typed into a field goes to all of them on
// Apply, and only the fields that were touched are written.
class MetadataPane : public QDockWidget
{
    Q_OBJECT
public:
    MetadataPane(CommandEngine &engine, QWidget *parent = nullptr);

    // Shown for these files (read after a moment, so arrowing through a
    // folder stays quick).
    void setFiles(const QStringList &paths);
    void loadNow(); // without waiting (tests)
    QStringList files() const { return m_files; }

    enum Field { Title, Artist, AlbumArtist, Album, Track, Disc, Year, Genre, Comment, Taken, Description, Copyright, FieldCount };
    QLineEdit *field(Field f) const { return m_edits[f]; }
    bool isFieldShown(Field f) const;
    bool hasChanges() const;
    QString statusText() const;

    // Writes the touched fields; returns how many files were written.
    int apply();
    void revert();

    static constexpr int kMaxFiles = 500;

Q_SIGNALS:
    void applied(const QStringList &paths);

private:
    void load();
    void updateButtons();

    CommandEngine &m_engine;
    QStringList m_files;    // selected
    QStringList m_editable; // those that can be written
    QLineEdit *m_edits[FieldCount] = {};
    QLabel *m_labels[FieldCount] = {};
    QHash<int, bool> m_touched;
    QFormLayout *m_form;
    QLabel *m_heading;
    QLabel *m_status;
    QPushButton *m_apply;
    QPushButton *m_revert;
    QTimer m_loadTimer;
    bool m_loading = false;
};
