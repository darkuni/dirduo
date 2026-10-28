#include "core/Prefs.h"

#include "core/Paths.h"
#include "core/StatusFormat.h"

#include <QSettings>

Prefs *Prefs::instance()
{
    static Prefs p;
    return &p;
}

Prefs::Prefs()
{
    reload();
}

void Prefs::reload()
{
    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("General"));
    Values v;
    v.statusCounts = s.value(QStringLiteral("statusCounts"), true).toBool();
    v.statusSelection = s.value(QStringLiteral("statusSelection"), true).toBool();
    v.statusFree = s.value(QStringLiteral("statusFree"), true).toBool();
    v.askOnError = s.value(QStringLiteral("askOnError"), true).toBool();
    v.junkHidden = s.value(QStringLiteral("junkHidden"), true).toBool();
    v.treeLines = s.value(QStringLiteral("treeLines"), true).toBool();
    v.treePath = s.value(QStringLiteral("treePath"), true).toBool();
    v.statusDefinition = s.value(QStringLiteral("statusDefinition")).toString();
    v.terminal = s.value(QStringLiteral("terminal")).toString();
    v.notifyJobs = s.value(QStringLiteral("notifyJobs"), true).toBool();
    v.notifyAfter = qBound(0, s.value(QStringLiteral("notifyAfter"), 10).toInt(), 3600);
    s.endGroup();
    s.beginGroup(QStringLiteral("Viewer"));
    Values::Viewer &w = v.viewer;
    const Values::Viewer d;
    w.openPictures = s.value(QStringLiteral("openPictures"), d.openPictures).toBool();
    w.openMedia = s.value(QStringLiteral("openMedia"), d.openMedia).toBool();
    w.siblings = s.value(QStringLiteral("siblings"), d.siblings).toBool();
    w.reuse = s.value(QStringLiteral("reuse"), d.reuse).toBool();
    w.wrap = s.value(QStringLiteral("wrap"), d.wrap).toBool();
    w.wheelSteps = s.value(QStringLiteral("wheelSteps"), d.wheelSteps).toBool();
    w.resetZoom = qBound(0, s.value(QStringLiteral("resetZoom"), d.resetZoom).toInt(), 2);
    w.background = qBound(0, s.value(QStringLiteral("background"), d.background).toInt(), 4);
    w.fullPathTitle = s.value(QStringLiteral("fullPathTitle"), d.fullPathTitle).toBool();
    w.playNext = s.value(QStringLiteral("playNext"), d.playNext).toBool();
    w.autoPlay = s.value(QStringLiteral("autoPlay"), d.autoPlay).toBool();
    w.slideSeconds = qBound(0.2, s.value(QStringLiteral("slideSeconds"), d.slideSeconds).toDouble(), 3600.0);
    w.slideRandom = s.value(QStringLiteral("slideRandom"), d.slideRandom).toBool();
    w.slideAuto = s.value(QStringLiteral("slideAuto"), d.slideAuto).toBool();
    w.slideFade = s.value(QStringLiteral("slideFade"), d.slideFade).toBool();
    w.markPane = s.value(QStringLiteral("markPane"), d.markPane).toBool();
    w.markCollection = s.value(QStringLiteral("markCollection"), d.markCollection).toString();
    w.openMarkedOnClose = s.value(QStringLiteral("openMarkedOnClose"), d.openMarkedOnClose).toBool();
    m_values = v;
}

void Prefs::setValues(const Values &v)
{
    m_values = v;
    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("General"));
    s.setValue(QStringLiteral("statusCounts"), v.statusCounts);
    s.setValue(QStringLiteral("statusSelection"), v.statusSelection);
    s.setValue(QStringLiteral("statusFree"), v.statusFree);
    s.setValue(QStringLiteral("askOnError"), v.askOnError);
    s.setValue(QStringLiteral("junkHidden"), v.junkHidden);
    s.setValue(QStringLiteral("treeLines"), v.treeLines);
    s.setValue(QStringLiteral("treePath"), v.treePath);
    s.setValue(QStringLiteral("statusDefinition"), v.statusDefinition);
    s.setValue(QStringLiteral("terminal"), v.terminal);
    s.setValue(QStringLiteral("notifyJobs"), v.notifyJobs);
    s.setValue(QStringLiteral("notifyAfter"), v.notifyAfter);
    s.endGroup();
    s.beginGroup(QStringLiteral("Viewer"));
    const Values::Viewer &w = v.viewer;
    s.setValue(QStringLiteral("openPictures"), w.openPictures);
    s.setValue(QStringLiteral("openMedia"), w.openMedia);
    s.setValue(QStringLiteral("siblings"), w.siblings);
    s.setValue(QStringLiteral("reuse"), w.reuse);
    s.setValue(QStringLiteral("wrap"), w.wrap);
    s.setValue(QStringLiteral("wheelSteps"), w.wheelSteps);
    s.setValue(QStringLiteral("resetZoom"), w.resetZoom);
    s.setValue(QStringLiteral("background"), w.background);
    s.setValue(QStringLiteral("fullPathTitle"), w.fullPathTitle);
    s.setValue(QStringLiteral("playNext"), w.playNext);
    s.setValue(QStringLiteral("autoPlay"), w.autoPlay);
    s.setValue(QStringLiteral("slideSeconds"), w.slideSeconds);
    s.setValue(QStringLiteral("slideRandom"), w.slideRandom);
    s.setValue(QStringLiteral("slideAuto"), w.slideAuto);
    s.setValue(QStringLiteral("slideFade"), w.slideFade);
    s.setValue(QStringLiteral("markPane"), w.markPane);
    s.setValue(QStringLiteral("markCollection"), w.markCollection);
    s.setValue(QStringLiteral("openMarkedOnClose"), w.openMarkedOnClose);
    Q_EMIT changed();
}

QString Prefs::Values::effectiveStatusDefinition() const
{
    return statusDefinition.trimmed().isEmpty() ? StatusFormat::defaultDefinition(statusCounts, statusSelection, statusFree)
                                                : statusDefinition;
}
