// Picture conversion: sizes, names, formats and the files written.
#include "core/ImageConvert.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

class TestImageConvert : public QObject
{
    Q_OBJECT

    QString makeImage(const QString &dir, const QString &name, const QSize &size, bool alpha = false)
    {
        QImage img(size, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        img.fill(alpha ? QColor(0, 0, 0, 0) : QColor(Qt::blue));
        // Red top-left pixel: shows where the picture's corner ends up.
        img.setPixelColor(0, 0, Qt::red);
        const QString path = QDir(dir).filePath(name);
        if (!img.save(path))
            qFatal("could not write %s", qPrintable(path));
        return path;
    }

private Q_SLOTS:
    void formats()
    {
        const QStringList w = ImageConvert::writableFormats();
        QVERIFY(w.contains(QStringLiteral("png")));
        QVERIFY(w.contains(QStringLiteral("jpg")));
        QVERIFY(w.contains(QStringLiteral("bmp")));
        QVERIFY(!w.contains(QStringLiteral("jpeg"))); // one name per format
        QCOMPARE(w.first(), QStringLiteral("png"));
        QCOMPARE(ImageConvert::normalizeFormat(QStringLiteral("JPEG")), QStringLiteral("jpg"));
        QCOMPARE(ImageConvert::normalizeFormat(QStringLiteral(".png")), QStringLiteral("png"));
        QVERIFY(ImageConvert::normalizeFormat(QStringLiteral("doc")).isEmpty());
        QCOMPARE(ImageConvert::displayName(QStringLiteral("jpeg")), QStringLiteral("JPEG"));
        QVERIFY(ImageConvert::hasQuality(QStringLiteral("jpg")));
        QVERIFY(!ImageConvert::hasQuality(QStringLiteral("png")));
        QVERIFY(!ImageConvert::hasAlpha(QStringLiteral("jpg")));
    }

    void sizes_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("w");
        QTest::addColumn<int>("h");
        QTest::addColumn<int>("pct");
        QTest::addColumn<bool>("noEnlarge");
        QTest::addColumn<int>("rotate");
        QTest::addColumn<QSize>("expected");
        // Source is 4000 x 3000 in every row.
        QTest::newRow("none") << int(ImageOptions::Resize::None) << 0 << 0 << 0 << true << 0 << QSize(4000, 3000);
        QTest::newRow("percent") << int(ImageOptions::Resize::Percent) << 0 << 0 << 25 << true << 0 << QSize(1000, 750);
        QTest::newRow("fit") << int(ImageOptions::Resize::Fit) << 1920 << 1080 << 0 << true << 0 << QSize(1440, 1080);
        QTest::newRow("width") << int(ImageOptions::Resize::Width) << 800 << 0 << 0 << true << 0 << QSize(800, 600);
        QTest::newRow("height") << int(ImageOptions::Resize::Height) << 0 << 300 << 0 << true << 0 << QSize(400, 300);
        QTest::newRow("exact") << int(ImageOptions::Resize::Exact) << 100 << 100 << 0 << true << 0 << QSize(100, 100);
        QTest::newRow("no enlarge") << int(ImageOptions::Resize::Width) << 8000 << 0 << 0 << true << 0 << QSize(4000, 3000);
        QTest::newRow("enlarge") << int(ImageOptions::Resize::Percent) << 0 << 0 << 200 << false << 0 << QSize(8000, 6000);
        QTest::newRow("turned") << int(ImageOptions::Resize::None) << 0 << 0 << 0 << true << 90 << QSize(3000, 4000);
        // Width applies to the picture after it is turned.
        QTest::newRow("turned width") << int(ImageOptions::Resize::Width) << 300 << 0 << 0 << true << 270 << QSize(300, 400);
    }

    void sizes()
    {
        QFETCH(int, mode);
        QFETCH(int, w);
        QFETCH(int, h);
        QFETCH(int, pct);
        QFETCH(bool, noEnlarge);
        QFETCH(int, rotate);
        QFETCH(QSize, expected);
        ImageOptions o;
        o.resize = ImageOptions::Resize(mode);
        o.width = w;
        o.height = h;
        o.percent = pct;
        o.noEnlarge = noEnlarge;
        o.rotate = rotate;
        QCOMPARE(ImageConvert::targetSize(QSize(4000, 3000), o), expected);
    }

    void planNames()
    {
        QTemporaryDir dir;
        const QString a = makeImage(dir.path(), QStringLiteral("shot.png"), QSize(40, 30));
        const QString b = makeImage(dir.path(), QStringLiteral("shot.bmp"), QSize(40, 30)); // same base name
        const QString c = makeImage(dir.path(), QStringLiteral("other.png"), QSize(40, 30));
        makeImage(dir.path(), QStringLiteral("other.jpg"), QSize(10, 10)); // already there
        QFile notPicture(QDir(dir.path()).filePath(QStringLiteral("notes.png")));
        QVERIFY(notPicture.open(QIODevice::WriteOnly));
        notPicture.write("hello");
        notPicture.close();

        ImageOptions o;
        o.format = QStringLiteral("jpg");
        auto plans = ImageConvert::plan({a, b, c, notPicture.fileName()}, o);
        QCOMPARE(plans.size(), 4);
        QCOMPARE(QFileInfo(plans[0].target).fileName(), QStringLiteral("shot.jpg"));
        QCOMPARE(QFileInfo(plans[1].target).fileName(), QStringLiteral("shot (2).jpg")); // never onto each other
        QCOMPARE(QFileInfo(plans[2].target).fileName(), QStringLiteral("other (2).jpg"));
        QVERIFY(plans[3].target.isEmpty());
        QVERIFY(!plans[3].problem.isEmpty());
        QCOMPARE(plans[0].sourceSize, QSize(40, 30));

        o.whenExists = ImageOptions::WhenExists::Skip;
        plans = ImageConvert::plan({c}, o);
        QVERIFY(plans[0].target.isEmpty());
        QVERIFY(plans[0].problem.contains(QStringLiteral("already exists")));

        o.whenExists = ImageOptions::WhenExists::Replace;
        plans = ImageConvert::plan({c}, o);
        QCOMPARE(QFileInfo(plans[0].target).fileName(), QStringLiteral("other.jpg"));

        // Keeping the format in the same folder: a copy, unless replacing.
        o.format.clear();
        o.whenExists = ImageOptions::WhenExists::Rename;
        plans = ImageConvert::plan({a}, o);
        QCOMPARE(QFileInfo(plans[0].target).fileName(), QStringLiteral("shot (2).png"));
        o.whenExists = ImageOptions::WhenExists::Replace;
        plans = ImageConvert::plan({a}, o);
        QCOMPARE(plans[0].target, QFileInfo(a).absoluteFilePath());
    }

    void convertWrites()
    {
        QTemporaryDir dir;
        const QString src = makeImage(dir.path(), QStringLiteral("big.png"), QSize(400, 200));
        const QString out = QDir(dir.path()).filePath(QStringLiteral("out/nested"));
        ImageOptions o;
        o.format = QStringLiteral("jpg");
        o.quality = 80;
        o.resize = ImageOptions::Resize::Width;
        o.width = 100;
        o.destFolder = out; // made on demand
        const auto plans = ImageConvert::plan({src}, o);
        const auto results = ImageConvert::run(plans, o);
        QCOMPARE(results.size(), 1);
        QVERIFY2(results[0].error.isEmpty(), qPrintable(results[0].error));
        QCOMPARE(results[0].target, QDir(out).filePath(QStringLiteral("big.jpg")));
        QImageReader r(results[0].target);
        QCOMPARE(r.format(), QByteArray("jpeg"));
        QCOMPARE(r.size(), QSize(100, 50));
        QVERIFY(QFileInfo::exists(src)); // the original stays
    }

    void rotateAndFlip()
    {
        QTemporaryDir dir;
        const QString src = makeImage(dir.path(), QStringLiteral("p.png"), QSize(40, 20));
        ImageOptions o;
        o.format = QStringLiteral("png");
        o.rotate = 90;
        o.destFolder = QDir(dir.path()).filePath(QStringLiteral("r"));
        auto results = ImageConvert::run(ImageConvert::plan({src}, o), o);
        QImage img(results[0].target);
        QCOMPARE(img.size(), QSize(20, 40));
        // Turned clockwise, the top-left corner goes to the top-right.
        QCOMPARE(img.pixelColor(19, 0), QColor(Qt::red));

        o.rotate = 0;
        o.flipH = true;
        o.destFolder = QDir(dir.path()).filePath(QStringLiteral("f"));
        results = ImageConvert::run(ImageConvert::plan({src}, o), o);
        img = QImage(results[0].target);
        QCOMPARE(img.size(), QSize(40, 20));
        QCOMPARE(img.pixelColor(39, 0), QColor(Qt::red));
        QCOMPARE(img.pixelColor(0, 0), QColor(Qt::blue));
    }

    void transparencyOnWhite()
    {
        QTemporaryDir dir;
        const QString src = makeImage(dir.path(), QStringLiteral("clear.png"), QSize(20, 20), true);
        ImageOptions o;
        o.format = QStringLiteral("bmp");
        const auto results = ImageConvert::run(ImageConvert::plan({src}, o), o);
        QVERIFY(results[0].error.isEmpty());
        const QImage img(results[0].target);
        QCOMPARE(img.pixelColor(10, 10), QColor(Qt::white));
    }

    void replaceInPlace()
    {
        QTemporaryDir dir;
        const QString src = makeImage(dir.path(), QStringLiteral("same.png"), QSize(64, 64));
        ImageOptions o;
        o.resize = ImageOptions::Resize::Percent;
        o.percent = 50;
        o.whenExists = ImageOptions::WhenExists::Replace;
        const auto results = ImageConvert::run(ImageConvert::plan({src}, o), o);
        QVERIFY(results[0].error.isEmpty());
        QCOMPARE(QImage(src).size(), QSize(32, 32));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }

    void cancel()
    {
        QTemporaryDir dir;
        QStringList files;
        for (int i = 0; i < 3; ++i)
            files << makeImage(dir.path(), QStringLiteral("c%1.png").arg(i), QSize(10, 10));
        ImageOptions o;
        o.format = QStringLiteral("bmp");
        ImageProgress progress;
        progress.cancel = true;
        const auto results = ImageConvert::run(ImageConvert::plan(files, o), o, &progress);
        QCOMPARE(results.size(), 3);
        for (const ImageResult &r : results)
            QVERIFY(r.skipped && r.target.isEmpty());
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("*.bmp")}, QDir::Files).size(), 0);
    }

    void names()
    {
        ImageOptions::Resize r;
        QVERIFY(ImageOptions::resizeFromName(QStringLiteral("Fit"), r));
        QCOMPARE(r, ImageOptions::Resize::Fit);
        QVERIFY(!ImageOptions::resizeFromName(QStringLiteral("huge"), r));
        ImageOptions::WhenExists w;
        QVERIFY(ImageOptions::whenExistsFromName(QStringLiteral("skip"), w));
        QCOMPARE(w, ImageOptions::WhenExists::Skip);
    }
};

QTEST_GUILESS_MAIN(TestImageConvert)
#include "tst_imageconvert.moc"
