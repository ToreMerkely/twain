#include <QtTest/QtTest>

#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QWheelEvent>

#include "ImageDiffView.h"

class TestImageDiffView : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString makePng(const QString& name, int w, int h, QColor color) {
        const QString path = m_dir.path() + "/" + name;
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(color);
        img.save(path, "PNG");
        return path;
    }

    QString makeTextFile(const QString& name) {
        const QString path = m_dir.path() + "/" + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("not an image\n");
        return path;
    }

    static void sendCtrlWheel(QWidget* target, int angleY) {
        QWheelEvent ev(QPointF(20, 20), QPointF(target->mapToGlobal(QPoint(20, 20))),
                       QPoint(), QPoint(0, angleY), Qt::NoButton, Qt::ControlModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &ev);
    }

private slots:
    void looks_like_image_by_extension();
    void set_files_loads_images_and_labels();
    void set_files_fails_on_undecodable_input();
    void set_files_accepts_missing_side();
    void ctrl_wheel_zooms_both_views_in_lockstep();
    void scrollbars_stay_in_sync();
};

void TestImageDiffView::looks_like_image_by_extension() {
    QVERIFY(ImageDiffView::looksLikeImage("a.png"));
    QVERIFY(ImageDiffView::looksLikeImage("/some/dir/B.JPG"));   // case-insensitive
    QVERIFY(ImageDiffView::looksLikeImage("photo.jpeg"));
    QVERIFY(ImageDiffView::looksLikeImage("anim.gif"));
    QVERIFY(ImageDiffView::looksLikeImage("scan.tiff"));
    QVERIFY(ImageDiffView::looksLikeImage("icon.ico"));
    QVERIFY(ImageDiffView::looksLikeImage("vector.svg"));
    QVERIFY(!ImageDiffView::looksLikeImage("notes.txt"));
    QVERIFY(!ImageDiffView::looksLikeImage("no-extension"));
    QVERIFY(!ImageDiffView::looksLikeImage("archive.png.gz"));
}

void TestImageDiffView::set_files_loads_images_and_labels() {
    const QString l = makePng("label-l.png", 100, 80, Qt::red);
    const QString r = makePng("label-r.png", 50, 40, Qt::blue);

    ImageDiffView v;
    v.resize(600, 400);
    QVERIFY(v.setFiles(l, r));
    QCOMPARE(v.leftPath(), l);
    QCOMPARE(v.rightPath(), r);

    // Path labels with pixel-size tooltips.
    bool sawLeft = false, sawRight = false;
    for (auto* lbl : v.findChildren<QLabel*>()) {
        if (lbl->text() == l) {
            sawLeft = true;
            QVERIFY(lbl->toolTip().contains("100x80 px"));
        }
        if (lbl->text() == r) {
            sawRight = true;
            QVERIFY(lbl->toolTip().contains("50x40 px"));
        }
    }
    QVERIFY(sawLeft);
    QVERIFY(sawRight);

    // Showing the widget performs the deferred fit-to-view.
    v.show();
    QVERIFY(v.isVisible());

    // Loading another pair while visible fits immediately.
    QVERIFY(v.setFiles(r, l));
    QCOMPARE(v.leftPath(), r);
}

void TestImageDiffView::set_files_fails_on_undecodable_input() {
    const QString png = makePng("ok.png", 10, 10, Qt::green);
    const QString txt = makeTextFile("bad.txt");

    ImageDiffView v;
    QString err;
    QVERIFY(!v.setFiles(txt, png, &err));
    QVERIFY2(err.contains("Could not decode"), qPrintable(err));

    err.clear();
    QVERIFY(!v.setFiles(png, txt, &err));
    QVERIFY2(err.contains("Could not decode"), qPrintable(err));

    // A failed load leaves the paths unset.
    QVERIFY(v.leftPath().isEmpty());
}

void TestImageDiffView::set_files_accepts_missing_side() {
    const QString png = makePng("one-sided.png", 30, 20, Qt::green);

    auto labelTexts = [](ImageDiffView& v) {
        QStringList texts;
        for (auto* lbl : v.findChildren<QLabel*>()) texts << lbl->text();
        return texts;
    };

    ImageDiffView v;
    v.resize(600, 400);
    QVERIFY(v.setFiles(QString(), png));
    QVERIFY(v.leftPath().isEmpty());
    QCOMPARE(v.rightPath(), png);
    QVERIFY(labelTexts(v).contains("(missing)"));
    QVERIFY(labelTexts(v).contains(png));
    v.show();

    QVERIFY(v.setFiles(png, QString()));
    QCOMPARE(v.leftPath(), png);
    QVERIFY(v.rightPath().isEmpty());
    QVERIFY(labelTexts(v).contains("(missing)"));
    QVERIFY(labelTexts(v).contains(png));
}

void TestImageDiffView::ctrl_wheel_zooms_both_views_in_lockstep() {
    const QString l = makePng("zoom-l.png", 100, 100, Qt::red);
    const QString r = makePng("zoom-r.png", 100, 100, Qt::blue);

    ImageDiffView v;
    v.resize(600, 400);
    v.show();
    QVERIFY(v.setFiles(l, r));

    auto views = v.findChildren<QGraphicsView*>();
    QCOMPARE(views.size(), 2);
    const double before0 = views[0]->transform().m11();
    const double before1 = views[1]->transform().m11();

    // Ctrl+wheel up zooms both panes by the same factor.
    sendCtrlWheel(views[0]->viewport(), 120);
    QVERIFY(qFuzzyCompare(views[0]->transform().m11(), before0 * 1.15));
    QVERIFY(qFuzzyCompare(views[1]->transform().m11(), before1 * 1.15));

    // Ctrl+wheel down zooms back out (pivoting on the other pane).
    sendCtrlWheel(views[1]->viewport(), -120);
    QVERIFY(qFuzzyCompare(views[0]->transform().m11(), before0));
    QVERIFY(qFuzzyCompare(views[1]->transform().m11(), before1));

    // A plain wheel is not a zoom.
    QWheelEvent plain(QPointF(20, 20),
                      QPointF(views[0]->viewport()->mapToGlobal(QPoint(20, 20))),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(views[0]->viewport(), &plain);
    QVERIFY(qFuzzyCompare(views[0]->transform().m11(), before0));
}

void TestImageDiffView::scrollbars_stay_in_sync() {
    const QString l = makePng("pan-l.png", 800, 800, Qt::red);
    const QString r = makePng("pan-r.png", 800, 800, Qt::blue);

    ImageDiffView v;
    v.resize(400, 250);
    v.show();
    QVERIFY(v.setFiles(l, r));

    auto views = v.findChildren<QGraphicsView*>();
    QCOMPARE(views.size(), 2);

    // Zoom in until the image overflows the viewport and scrollbars engage.
    for (int i = 0; i < 10 && views[0]->horizontalScrollBar()->maximum() == 0; ++i) {
        sendCtrlWheel(views[0]->viewport(), 120);
    }
    QVERIFY(views[0]->horizontalScrollBar()->maximum() > 0);
    QVERIFY(views[0]->verticalScrollBar()->maximum() > 0);

    views[0]->horizontalScrollBar()->setValue(views[0]->horizontalScrollBar()->maximum());
    QCOMPARE(views[1]->horizontalScrollBar()->value(),
             views[0]->horizontalScrollBar()->value());
    views[1]->verticalScrollBar()->setValue(views[1]->verticalScrollBar()->maximum());
    QCOMPARE(views[0]->verticalScrollBar()->value(),
             views[1]->verticalScrollBar()->value());
}

QTEST_MAIN(TestImageDiffView)
#include "test_imagediffview.moc"
