#include <QtTest/QtTest>

#include <QMouseEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QWheelEvent>

#include "DiffPane.h"
#include "DiffView.h"

namespace {

DiffRow contentRow(int srcLine, const QString& text, Diff::Op kind) {
    DiffRow r;
    r.sourceLine = srcLine;
    r.text = text;
    r.kind = kind;
    r.filler = false;
    return r;
}

DiffRow fillerRow(Diff::Op kind) {
    DiffRow r;
    r.sourceLine = -1;
    r.text = QString();
    r.kind = kind;
    r.filler = true;
    return r;
}

DiffRow markerRow(const QString& text) {
    DiffRow r;
    r.sourceLine = -1;
    r.text = text;
    r.kind = Diff::Op::Equal;
    r.filler = true;
    r.isTruncationMarker = true;
    return r;
}

// Viewport y coordinate of the middle of `row` (also valid for the line
// number area, which scrolls in lockstep with the viewport).
int rowMidY(DiffPane& pane, int row) {
    QTextCursor c(pane.document()->findBlockByNumber(row));
    pane.setTextCursor(c);
    return pane.cursorRect().center().y();
}

}  // namespace

class TestDiffPane : public QObject {
    Q_OBJECT
private slots:
    void extract_content_skips_pristine_fillers_and_markers();
    void source_line_and_filler_lookup();
    void search_is_case_insensitive_and_cycles();
    void save_and_restore_cursor_skip_fillers();
    void partial_selection_paints_and_clears_backgrounds();
    void truncation_marker_click_emits_signal();
    void escape_key_requests_partial_clear();
    void line_number_area_clicks_emit_signals();
    void font_point_size_updates();
    void ctrl_wheel_zooms_shift_wheel_scrolls_horizontally();
    void painting_smoke_test();
};

void TestDiffPane::extract_content_skips_pristine_fillers_and_markers() {
    DiffPane pane;
    pane.setRows({
        contentRow(0, "alpha", Diff::Op::Equal),
        fillerRow(Diff::Op::Delete),
        contentRow(1, "beta", Diff::Op::Insert),
        markerRow("click to load more"),
    });
    QCOMPARE(pane.extractContent(), QStringList({"alpha", "beta"}));
}

void TestDiffPane::source_line_and_filler_lookup() {
    DiffPane pane;
    pane.setRows({
        contentRow(0, "alpha", Diff::Op::Equal),
        fillerRow(Diff::Op::Delete),
    });
    QCOMPARE(pane.sourceLineAtRow(0), 0);
    QCOMPARE(pane.sourceLineAtRow(1), -1);
    QVERIFY(!pane.isRowFiller(0));
    QVERIFY(pane.isRowFiller(1));
    // Out-of-range queries are safe.
    QCOMPARE(pane.sourceLineAtRow(-1), -1);
    QCOMPARE(pane.sourceLineAtRow(99), -1);
    QVERIFY(!pane.isRowFiller(99));
}

void TestDiffPane::search_is_case_insensitive_and_cycles() {
    DiffPane pane;
    pane.setRows({
        contentRow(0, "Apple pie", Diff::Op::Equal),
        contentRow(1, "apple tart", Diff::Op::Equal),
        contentRow(2, "banana", Diff::Op::Equal),
    });

    pane.setSearchTerm("APPLE");
    QCOMPARE(pane.matchCount(), 2);
    QCOMPARE(pane.currentMatchIndex(), -1);

    QVERIFY(pane.findNext());
    QCOMPARE(pane.currentMatchIndex(), 0);
    QVERIFY(pane.findNext());
    QCOMPARE(pane.currentMatchIndex(), 1);
    QVERIFY(pane.findNext());  // wraps
    QCOMPARE(pane.currentMatchIndex(), 0);
    QVERIFY(pane.findPrev());  // wraps backwards
    QCOMPARE(pane.currentMatchIndex(), 1);
    QVERIFY(pane.findPrev());
    QCOMPARE(pane.currentMatchIndex(), 0);

    pane.setSearchTerm(QString());
    QCOMPARE(pane.matchCount(), 0);
    QVERIFY(!pane.findNext());
    QVERIFY(!pane.findPrev());
}

void TestDiffPane::save_and_restore_cursor_skip_fillers() {
    DiffPane pane;
    pane.setRows({
        contentRow(0, "one", Diff::Op::Equal),
        fillerRow(Diff::Op::Delete),
        contentRow(1, "two", Diff::Op::Equal),
    });

    // Cursor on "two" (block 2), column 1. The filler above is not part of
    // the file, so the saved position is file line 1.
    QTextBlock blk = pane.document()->findBlockByNumber(2);
    QTextCursor c(blk);
    c.setPosition(blk.position() + 1);
    pane.setTextCursor(c);
    const auto ctx = pane.saveCursor();
    QCOMPARE(ctx.fileLine, 1);
    QCOMPARE(ctx.column, 1);

    // Move away, then restore: back to block 2, column 1.
    pane.setTextCursor(QTextCursor(pane.document()->firstBlock()));
    pane.restoreCursor(ctx);
    QCOMPARE(pane.textCursor().blockNumber(), 2);
    QCOMPARE(pane.textCursor().positionInBlock(), 1);

    // A cursor parked on a pristine filler snaps to the next real line.
    pane.setTextCursor(QTextCursor(pane.document()->findBlockByNumber(1)));
    const auto fillerCtx = pane.saveCursor();
    QCOMPARE(fillerCtx.fileLine, 1);
    QCOMPARE(fillerCtx.column, 0);

    // Restoring a context that matches no row is a no-op.
    DiffPane::CursorContext bogus;
    bogus.fileLine = 42;
    pane.restoreCursor(bogus);
    QCOMPARE(pane.textCursor().blockNumber(), 1);
}

void TestDiffPane::partial_selection_paints_and_clears_backgrounds() {
    DiffPane pane;
    pane.setRows({
        contentRow(0, "changed a", Diff::Op::Delete),
        contentRow(1, "changed b", Diff::Op::Delete),
    });

    auto bgColor = [&](int row) {
        return pane.document()->findBlockByNumber(row).blockFormat().background().color();
    };
    const QColor deleteBg = bgColor(0);
    QVERIFY(deleteBg.isValid());

    pane.setRowPartial(0, /*selected=*/true, /*neutral=*/false);
    QCOMPARE(bgColor(0), QColor(200, 220, 255));

    pane.setRowPartial(1, /*selected=*/false, /*neutral=*/true);
    // Neutral rows suppress the kind colour entirely.
    QCOMPARE(pane.document()->findBlockByNumber(1).blockFormat().background().style(),
             Qt::NoBrush);

    // Same-state call and out-of-range rows are safe no-ops.
    pane.setRowPartial(0, true, false);
    pane.setRowPartial(99, true, false);
    QCOMPARE(bgColor(0), QColor(200, 220, 255));

    pane.clearAllPartial();
    QCOMPARE(bgColor(0), deleteBg);
    QCOMPARE(bgColor(1), deleteBg);

    // clearAllPartial with nothing selected is a no-op.
    pane.clearAllPartial();
    QCOMPARE(bgColor(0), deleteBg);
}

void TestDiffPane::truncation_marker_click_emits_signal() {
    DiffPane pane;
    pane.resize(400, 200);
    pane.show();
    pane.setRows({
        markerRow("click to load more"),
        contentRow(0, "regular line", Diff::Op::Equal),
    });

    pane.setTextCursor(QTextCursor(pane.document()->firstBlock()));
    QVERIFY(pane.isCursorOnTruncationMarker());
    pane.setTextCursor(QTextCursor(pane.document()->findBlockByNumber(1)));
    QVERIFY(!pane.isCursorOnTruncationMarker());

    QSignalSpy markerSpy(&pane, &DiffPane::truncationMarkerClicked);
    QSignalSpy clearSpy(&pane, &DiffPane::clearPartialRequested);

    // Click on the marker row.
    QTest::mouseClick(pane.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(10, rowMidY(pane, 0)));
    QCOMPARE(markerSpy.count(), 1);

    // Click on a regular row requests a partial-selection clear instead.
    QTest::mouseClick(pane.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(10, rowMidY(pane, 1)));
    QCOMPARE(markerSpy.count(), 1);
    QVERIFY(clearSpy.count() >= 1);
}

void TestDiffPane::escape_key_requests_partial_clear() {
    DiffPane pane;
    pane.setRows({contentRow(0, "changed", Diff::Op::Delete)});

    QSignalSpy clearSpy(&pane, &DiffPane::clearPartialRequested);
    QTest::keyClick(&pane, Qt::Key_Escape);
    QCOMPARE(clearSpy.count(), 1);

    // With an active partial selection Escape is consumed by the pane.
    pane.setRowPartial(0, true, false);
    QTest::keyClick(&pane, Qt::Key_Escape);
    QCOMPARE(clearSpy.count(), 2);
}

void TestDiffPane::line_number_area_clicks_emit_signals() {
    DiffPane pane;
    pane.resize(500, 300);
    pane.show();
    pane.setRows({
        contentRow(0, "deleted line", Diff::Op::Delete),
        contentRow(1, "equal line", Diff::Op::Equal),
        fillerRow(Diff::Op::Insert),
    });

    QSignalSpy arrowSpy(&pane, &DiffPane::arrowClicked);
    QSignalSpy lineSpy(&pane, &DiffPane::lineNumberClicked);

    const int numberZoneX = pane.lineNumberAreaWidth() - 6;

    auto clickArea = [&](int x, int row, Qt::KeyboardModifiers mods) {
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(x, rowMidY(pane, row)),
                       QPointF(pane.mapToGlobal(QPoint(x, rowMidY(pane, row)))),
                       Qt::LeftButton, Qt::LeftButton, mods);
        pane.lineNumberAreaMousePressEvent(&ev);
    };

    // Arrow zone on a non-Equal row fires arrowClicked.
    clickArea(5, 0, Qt::NoModifier);
    QCOMPARE(arrowSpy.count(), 1);
    QCOMPARE(arrowSpy.last().at(0).toInt(), 0);

    // Arrow zone on an Equal row does nothing.
    clickArea(5, 1, Qt::NoModifier);
    QCOMPARE(arrowSpy.count(), 1);

    // Number zone on a real row fires lineNumberClicked (with shift state).
    clickArea(numberZoneX, 0, Qt::NoModifier);
    QCOMPARE(lineSpy.count(), 1);
    QCOMPARE(lineSpy.last().at(0).toInt(), 0);
    QCOMPARE(lineSpy.last().at(1).toBool(), false);

    clickArea(numberZoneX, 1, Qt::ShiftModifier);
    QCOMPARE(lineSpy.count(), 2);
    QCOMPARE(lineSpy.last().at(1).toBool(), true);

    // Number zone on a filler row does nothing.
    clickArea(numberZoneX, 2, Qt::NoModifier);
    QCOMPARE(lineSpy.count(), 2);
}

void TestDiffPane::font_point_size_updates() {
    DiffPane pane;
    const int original = pane.font().pointSize();
    pane.setDiffFontPointSize(original + 4);
    QCOMPARE(pane.font().pointSize(), original + 4);
    // Same size again is an early-return.
    pane.setDiffFontPointSize(original + 4);
    QCOMPARE(pane.font().pointSize(), original + 4);
    pane.setDiffFontPointSize(original);
}

void TestDiffPane::ctrl_wheel_zooms_shift_wheel_scrolls_horizontally() {
    const int originalPt = DiffView::diffFontPt();

    DiffPane pane;
    pane.resize(200, 150);
    pane.show();
    pane.setRows({contentRow(0, QString(500, 'x'), Diff::Op::Equal)});

    // Ctrl+wheel up bumps the app-wide diff font size.
    {
        QWheelEvent ev(QPointF(20, 20), QPointF(pane.mapToGlobal(QPoint(20, 20))),
                       QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(pane.viewport(), &ev);
    }
    QCOMPARE(DiffView::diffFontPt(), originalPt + 1);
    DiffView::setDiffFontPt(originalPt);

    // Shift+wheel scrolls horizontally.
    QScrollBar* hbar = pane.horizontalScrollBar();
    QVERIFY(hbar->maximum() > 0);
    hbar->setValue(hbar->maximum());
    const int before = hbar->value();
    {
        QWheelEvent ev(QPointF(20, 20), QPointF(pane.mapToGlobal(QPoint(20, 20))),
                       QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(pane.viewport(), &ev);
    }
    QVERIFY(hbar->value() < before);
}

void TestDiffPane::painting_smoke_test() {
    // Exercise paintEvent (filler hatch) and lineNumberAreaPaintEvent
    // (brackets, arrows, partial markers, copied bar) for both sides.
    for (auto side : {DiffPane::Side::Left, DiffPane::Side::Right}) {
        DiffPane pane;
        pane.setSide(side);
        pane.resize(500, 400);

        DiffRow segRow = contentRow(1, "delta line", Diff::Op::Delete);
        segRow.segments = {{0, 5, true}, {5, 5, false}};
        DiffRow copiedRow = contentRow(4, "copied", Diff::Op::Equal);
        copiedRow.recentlyCopied = true;

        pane.setRows({
            contentRow(0, "equal line", Diff::Op::Equal),
            segRow,
            contentRow(2, "delta two", Diff::Op::Delete),
            fillerRow(Diff::Op::Insert),
            copiedRow,
            markerRow("load more"),
        });
        // Multi-row partial selection (start/mid/end markers in the gutter).
        pane.setRowPartial(1, true, false);
        pane.setRowPartial(2, true, false);

        pane.show();
        pane.repaint();
        QVERIFY(pane.isVisible());
    }
}

int main(int argc, char* argv[]) {
    // Keep QSettings (diff font size) out of the user's real config.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication::setAttribute(Qt::AA_Use96Dpi, true);
    QApplication app(argc, argv);
    TestDiffPane tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_diffpane.moc"
