#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Filters kept under a name, as Opus's saved filters: "Disc images" for
// *.iso;*.chd;*.cue. They go in the filter bar, Set FILTER=@name and
// Select PATTERN=@name. Saved in ~/.config/dirduo/filters.json; a few
// come ready-made until the file exists.
class SavedFilters : public QObject
{
    Q_OBJECT
public:
    struct Filter
    {
        QString name;
        QString pattern;           // as typed in the filter bar
        bool hide = false;         // hide what matches instead
        QString scope = QStringLiteral("files"); // files, folders or both
        bool operator==(const Filter &o) const
        {
            return name == o.name && pattern == o.pattern && hide == o.hide && scope == o.scope;
        }
    };

    static SavedFilters *instance();
    QString file() const;
    void reload();

    QList<Filter> filters() const { return m_filters; }
    bool has(const QString &name) const;
    Filter filter(const QString &name) const; // empty name when there is none
    void setFilters(const QList<Filter> &filters);
    // Adds it, or replaces the one with its name.
    void save(const Filter &f);
    bool remove(const QString &name);

    static QList<Filter> defaults();
    // "@Disc images" -> its pattern (and whether it hides); anything else as it is.
    QString resolve(const QString &text, bool *hide = nullptr) const;

Q_SIGNALS:
    void changed();

private:
    SavedFilters();
    void write();
    QList<Filter> m_filters;
};
