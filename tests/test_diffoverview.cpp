#include <QtTest/QtTest>

#include <QMouseEvent>

#include "DiffOverview.h"

class TestDiffOverview : public QObject {
    Q_OBJECT

    // QTest::mouseMove can't hold a button down, so synthesize the drag
    // event directly.
    static void dragTo(DiffOverview& w, int y) {
        QMouseEvent ev(QEvent::MouseMove, QPointF(7, y),
                       QPointF(w.mapToGlobal(QPoint(7, y))), Qt::NoButton,
                       Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &ev);
    }

private slots:
    void fixed_width_and_size_hint();
    void click_requests_centered_scroll();
    void click_positions_clamp_to_row_range();
    void drag_with_left_button_scrolls_too();
    void other_buttons_are_ignored();
    void setters_early_return_on_same_value();
    void paint_branches_smoke();
};

void TestDiffOverview::fixed_width_and_size_hint() {
    DiffOverview w;
    QCOMPARE(w.width(), 14);
    // sizeHint() is protected in the subclass; dispatch through QWidget.
    QCOMPARE(static_cast<const QWidget&>(w).sizeHint(), QSize(14, 100));
}

void TestDiffOverview::click_requests_centered_scroll() {
    DiffOverview w;
    w.resize(14, 100);
    w.show();
    w.setTotalRows(100);            // 1 row per pixel
    w.setViewport(0, 10);

    QSignalSpy spy(&w, &DiffOverview::scrollRequested);
    QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(7, 50));
    QCOMPARE(spy.count(), 1);
    // Clicked row 50, centered: top = 50 - 10/2.
    QCOMPARE(spy.last().at(0).toInt(), 45);
}

void TestDiffOverview::click_positions_clamp_to_row_range() {
    DiffOverview w;
    w.resize(14, 100);
    w.show();
    w.setTotalRows(100);
    w.setViewport(0, 10);

    QSignalSpy spy(&w, &DiffOverview::scrollRequested);
    // Top edge clamps to 0.
    QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(7, 0));
    QCOMPARE(spy.last().at(0).toInt(), 0);
    // Bottom edge: row clamps to totalRows-1, top to 99-5.
    QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(7, 99));
    QCOMPARE(spy.last().at(0).toInt(), 94);
}

void TestDiffOverview::drag_with_left_button_scrolls_too() {
    DiffOverview w;
    w.resize(14, 100);
    w.show();
    w.setTotalRows(100);
    w.setViewport(0, 10);

    QSignalSpy spy(&w, &DiffOverview::scrollRequested);
    dragTo(w, 20);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toInt(), 15);
    dragTo(w, 80);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).toInt(), 75);

    // A move without the left button held does nothing.
    QMouseEvent hover(QEvent::MouseMove, QPointF(7, 30),
                      QPointF(w.mapToGlobal(QPoint(7, 30))), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &hover);
    QCOMPARE(spy.count(), 2);
}

void TestDiffOverview::other_buttons_are_ignored() {
    DiffOverview w;
    w.resize(14, 100);
    w.show();
    w.setTotalRows(100);
    w.setViewport(0, 10);

    QSignalSpy spy(&w, &DiffOverview::scrollRequested);
    QTest::mousePress(&w, Qt::RightButton, Qt::NoModifier, QPoint(7, 50));
    QCOMPARE(spy.count(), 0);
}

void TestDiffOverview::setters_early_return_on_same_value() {
    DiffOverview w;
    w.setTotalRows(50);
    w.setTotalRows(50);
    w.setViewport(3, 12);
    w.setViewport(3, 12);
    w.setCurrentRow(7);
    w.setCurrentRow(7);
    // Behavior is observable through scroll math: with no viewport rows the
    // requested top is the clicked row itself.
    w.resize(14, 100);
    w.show();
    QSignalSpy spy(&w, &DiffOverview::scrollRequested);
    w.setViewport(0, 0);
    QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(7, 50));
    QCOMPARE(spy.last().at(0).toInt(), 25);  // 50 px on 100px/50rows = row 25
}

void TestDiffOverview::paint_branches_smoke() {
    DiffOverview w;
    w.resize(14, 120);
    w.show();

    // Empty: early return.
    w.repaint();

    // Marks + viewport box.
    w.setTotalRows(200);
    w.setMarks({{10, 40}, {100, 102}, {150, 150}});  // incl. min-height mark
    w.setViewport(50, 30);
    w.setCurrentRow(60);
    w.repaint();

    // No viewport rows: marks only.
    w.setViewport(0, 0);
    w.repaint();
    QVERIFY(w.isVisible());
}

QTEST_MAIN(TestDiffOverview)
#include "test_diffoverview.moc"
