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
    s.endGroup();
    s.beginGroup(QStringLiteral("Thumbnails"));
    Values::Thumbs &t = v.thumbs;
    const Values::Thumbs td;
    t.size = qBound(32, s.value(QStringLiteral("size"), td.size).toInt(), 512);
    t.resize = qBound(0, s.value(QStringLiteral("resize"), td.resize).toInt(), 1);
    t.frame = qBound(0, s.value(QStringLiteral("frame"), td.frame).toInt(), 4);
    t.spacing = qBound(0, s.value(QStringLiteral("spacing"), td.spacing).toInt(), 64);
    t.spread = s.value(QStringLiteral("spread"), td.spread).toBool();
    t.slider = s.value(QStringLiteral("slider"), td.slider).toBool();
    t.labelLines = qBound(0, s.value(QStringLiteral("labelLines"), td.labelLines).toInt(), 3);
    t.labelDetails = s.value(QStringLiteral("labelDetails"), td.labelDetails).toBool();
    t.labelDate = s.value(QStringLiteral("labelDate"), td.labelDate).toBool();
    t.sprockets = s.value(QStringLiteral("sprockets"), td.sprockets).toBool();
    t.stars = s.value(QStringLiteral("stars"), td.stars).toBool();
    t.typeIcon = s.value(QStringLiteral("typeIcon"), td.typeIcon).toBool();
    t.dimensionBars = s.value(QStringLiteral("dimensionBars"), td.dimensionBars).toBool();
    t.video = s.value(QStringLiteral("video"), td.video).toBool();
    t.documents = s.value(QStringLiteral("documents"), td.documents).toBool();
    t.covers = s.value(QStringLiteral("covers"), td.covers).toBool();
    t.folders = s.value(QStringLiteral("folders"), td.folders).toBool();
    t.folderNewest = s.value(QStringLiteral("folderNewest"), td.folderNewest).toBool();
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
    s.endGroup();
    s.beginGroup(QStringLiteral("Thumbnails"));
    const Values::Thumbs &t = v.thumbs;
    s.setValue(QStringLiteral("size"), t.size);
    s.setValue(QStringLiteral("resize"), t.resize);
    s.setValue(QStringLiteral("frame"), t.frame);
    s.setValue(QStringLiteral("spacing"), t.spacing);
    s.setValue(QStringLiteral("spread"), t.spread);
    s.setValue(QStringLiteral("slider"), t.slider);
    s.setValue(QStringLiteral("labelLines"), t.labelLines);
    s.setValue(QStringLiteral("labelDetails"), t.labelDetails);
    s.setValue(QStringLiteral("labelDate"), t.labelDate);
    s.setValue(QStringLiteral("sprockets"), t.sprockets);
    s.setValue(QStringLiteral("stars"), t.stars);
    s.setValue(QStringLiteral("typeIcon"), t.typeIcon);
    s.setValue(QStringLiteral("dimensionBars"), t.dimensionBars);
    s.setValue(QStringLiteral("video"), t.video);
    s.setValue(QStringLiteral("documents"), t.documents);
    s.setValue(QStringLiteral("covers"), t.covers);
    s.setValue(QStringLiteral("folders"), t.folders);
    s.setValue(QStringLiteral("folderNewest"), t.folderNewest);
    Q_EMIT changed();
}

QString Prefs::Values::effectiveStatusDefinition() const
{
    return statusDefinition.trimmed().isEmpty() ? StatusFormat::defaultDefinition(statusCounts, statusSelection, statusFree)
                                                : statusDefinition;
}
