#include <QtTest/QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolButton>

#include "DiffOverview.h"
#include "DiffPane.h"
#include "DiffView.h"

class TestDiffView : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString writeFile(const QString& name, const QByteArray& content) {
        const QString path = m_dir.path() + "/" + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
        f.write(content);
        return path;
    }

    static QByteArray readFile(const QString& path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }

    // The pane whose document contains `needle` (left/right are
    // indistinguishable by type, so identify them by content).
    static DiffPane* paneContaining(DiffView& dv, const QString& needle) {
        for (auto* p : dv.findChildren<DiffPane*>()) {
            if (p->toPlainText().contains(needle)) return p;
        }
        return nullptr;
    }

private slots:
    void identical_files_have_zero_differences();
    void changed_line_produces_one_difference();
    void missing_file_reports_error();
    void binary_file_reports_error();
    void empty_left_path_is_pure_insert();
    void next_prev_difference_navigation();
    void tall_block_scrolls_before_jumping();
    void scroll_sync_and_overview_requests();
    void bottom_line_follows_horizontal_scroll();
    void ignore_case_option();
    void ignore_whitespace_option();
    void ignore_blank_lines_option();
    void editing_pane_sets_dirty_and_save_writes();
    void arrow_copy_and_undo();
    void partial_row_copy();
    void partial_lone_row_click_toggles();
    void search_bar_interactions();
    void font_size_adjustments_propagate();
    void splitter_state_round_trip();
    void truncated_file_loads_more();
    void external_file_change_reloads();
};

void TestDiffView::identical_files_have_zero_differences() {
    const QString l = writeFile("id-l.txt", "a\nb\nc\n");
    const QString r = writeFile("id-r.txt", "a\nb\nc\n");
    DiffView dv;
    QSignalSpy spy(&dv, &DiffView::currentDifferenceChanged);
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 0);
    QCOMPARE(dv.currentDifference(), -1);
    QCOMPARE(dv.leftPath(), l);
    QCOMPARE(dv.rightPath(), r);
    QVERIFY(!dv.isDirty());
    QVERIFY(!dv.isAnyFileTruncated());
    QVERIFY(!dv.canUndoArrow());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toInt(), -1);
    QCOMPARE(spy.last().at(1).toInt(), 0);
    // prev/undo with nothing to do are no-ops.
    dv.prevDifference();
    dv.undoArrow();
    QCOMPARE(dv.currentDifference(), -1);
}

void TestDiffView::changed_line_produces_one_difference() {
    const QString l = writeFile("ch-l.txt", "a\nx one\nc\n");
    const QString r = writeFile("ch-r.txt", "a\nx two\nc\n");
    DiffView dv;
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 1);
}

void TestDiffView::missing_file_reports_error() {
    const QString r = writeFile("miss-r.txt", "a\n");
    DiffView dv;
    QString err;
    QVERIFY(!dv.setFiles(m_dir.path() + "/does-not-exist", r, &err));
    QVERIFY2(err.contains("Could not open"), qPrintable(err));
}

void TestDiffView::binary_file_reports_error() {
    const QString l = writeFile("bin-l.bin", QByteArray("ab\0cd", 5));
    const QString r = writeFile("bin-r.txt", "a\n");
    DiffView dv;
    QString err;
    QVERIFY(!dv.setFiles(l, r, &err));
    QVERIFY2(err.contains("binary"), qPrintable(err));

    // Binary on the right side is caught too.
    err.clear();
    QVERIFY(!dv.setFiles(r, l, &err));
    QVERIFY2(err.contains("binary"), qPrintable(err));
}

void TestDiffView::empty_left_path_is_pure_insert() {
    const QString r = writeFile("ins-r.txt", "a\nb\n");
    DiffView dv;
    QVERIFY(dv.setFiles(QString(), r));
    QCOMPARE(dv.differenceCount(), 1);
    // save() skips the empty-path side and still succeeds.
    QVERIFY(dv.save());
    QCOMPARE(readFile(r), QByteArray("a\nb\n"));
}

void TestDiffView::next_prev_difference_navigation() {
    const QString l = writeFile("nav-l.txt", "same0\nalpha one\nsame2\nbeta one\nsame4\n");
    const QString r = writeFile("nav-r.txt", "same0\nalpha two\nsame2\nbeta two\nsame4\n");
    DiffView dv;
    dv.resize(900, 700);
    dv.show();
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 2);

    QSignalSpy spy(&dv, &DiffView::currentDifferenceChanged);
    dv.nextDifference();
    QCOMPARE(dv.currentDifference(), 0);
    dv.nextDifference();
    QCOMPARE(dv.currentDifference(), 1);
    // Past the last difference nothing changes.
    dv.nextDifference();
    QCOMPARE(dv.currentDifference(), 1);

    dv.prevDifference();
    QCOMPARE(dv.currentDifference(), 0);
    // Before the first difference nothing changes.
    dv.prevDifference();
    QCOMPARE(dv.currentDifference(), 0);

    QCOMPARE(spy.count(), 3);  // goToDiff emissions only
}

void TestDiffView::tall_block_scrolls_before_jumping() {
    // A change block taller than the viewport: the first Ctrl+N enters it,
    // the second jumps to its bottom edge (not the next diff), the third
    // finally moves on to the next block.
    QByteArray left = "context\n";
    QByteArray right = "context\n";
    for (int i = 0; i < 100; ++i) {
        left += QString("line %1 left\n").arg(i).toUtf8();
        right += QString("line %1 right\n").arg(i).toUtf8();
    }
    left += "middle\nend one\n";
    right += "middle\nend two\n";

    const QString l = writeFile("tall-l.txt", left);
    const QString r = writeFile("tall-r.txt", right);
    DiffView dv;
    dv.resize(500, 200);  // small viewport: ~12 visible lines
    dv.show();
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 2);

    dv.nextDifference();
    QCOMPARE(dv.currentDifference(), 0);
    dv.nextDifference();  // still inside the tall block: scrolls to its end
    QCOMPARE(dv.currentDifference(), 0);
    dv.nextDifference();
    QCOMPARE(dv.currentDifference(), 1);
}

void TestDiffView::scroll_sync_and_overview_requests() {
    QByteArray body;
    for (int i = 0; i < 200; ++i) body += QString("row %1\n").arg(i).toUtf8();
    const QString l = writeFile("sync-l.txt", body);
    const QString r = writeFile("sync-r.txt", body);
    DiffView dv;
    dv.resize(500, 200);
    dv.show();
    QVERIFY(dv.setFiles(l, r));

    auto panes = dv.findChildren<DiffPane*>();
    QCOMPARE(panes.size(), 2);

    // Vertical scrolling of one pane drags the other along.
    panes[0]->verticalScrollBar()->setValue(7);
    QCOMPARE(panes[1]->verticalScrollBar()->value(), 7);
    panes[1]->verticalScrollBar()->setValue(3);
    QCOMPARE(panes[0]->verticalScrollBar()->value(), 3);

    // The overview strip can request an absolute scroll position.
    auto* overview = dv.findChild<DiffOverview*>();
    QVERIFY(overview);
    QVERIFY(QMetaObject::invokeMethod(overview, "scrollRequested", Q_ARG(int, 11)));
    QCOMPARE(panes[0]->verticalScrollBar()->value(), 11);
    QCOMPARE(panes[1]->verticalScrollBar()->value(), 11);
}

void TestDiffView::bottom_line_follows_horizontal_scroll() {
    const QByteArray tail(400, 'x');
    const QString l = writeFile("hs-l.txt", "left " + tail + "\n");
    const QString r = writeFile("hs-r.txt", "right " + tail + "\n");
    DiffView dv;
    dv.resize(500, 200);
    dv.show();
    QVERIFY(dv.setFiles(l, r));

    auto panes = dv.findChildren<DiffPane*>();
    QCOMPARE(panes.size(), 2);
    QList<QPlainTextEdit*> bottom;
    for (auto* e : dv.findChildren<QPlainTextEdit*>()) {
        if (!qobject_cast<DiffPane*>(e)) bottom.append(e);
    }
    QCOMPARE(bottom.size(), 2);

    // Selecting the line fills the bottom panes.
    panes[0]->setTextCursor(QTextCursor(panes[0]->document()->firstBlock()));
    QVERIFY(bottom[0]->toPlainText().startsWith("left "));

    // The bottom panes start at the same column as the diff panes, despite
    // rendering in a smaller font.
    const qreal paneAdv = QFontMetricsF(panes[0]->font()).horizontalAdvance(' ');
    const qreal bottomAdv = QFontMetricsF(bottom[0]->font()).horizontalAdvance(' ');
    QVERIFY(paneAdv > 0 && bottomAdv > 0);
    panes[0]->horizontalScrollBar()->setValue(qRound(paneAdv * 60));
    for (auto* b : bottom) {
        QVERIFY(b->horizontalScrollBar()->maximum() > 0);
        QCOMPARE(qRound(b->horizontalScrollBar()->value() / bottomAdv), 60);
    }

    // Re-selecting the line must not throw the offset away. Put the cursor on a
    // column that is already visible, so the pane itself does not scroll.
    QTextCursor c(panes[1]->document()->firstBlock());
    c.setPosition(c.block().position() + 65);
    panes[1]->setTextCursor(c);
    for (auto* b : bottom) {
        QCOMPARE(qRound(b->horizontalScrollBar()->value() / bottomAdv), 60);
    }
}

void TestDiffView::ignore_case_option() {
    const QString l = writeFile("case-l.txt", "HELLO World\n");
    const QString r = writeFile("case-r.txt", "hello world\n");
    DiffView dv;
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 1);

    DiffView::Options opts;
    opts.ignoreCase = true;
    dv.setOptions(opts);
    QVERIFY(dv.options().ignoreCase);
    QCOMPARE(dv.differenceCount(), 0);
}

void TestDiffView::ignore_whitespace_option() {
    const QString l = writeFile("ws-l.txt", "a   b\n");
    const QString r = writeFile("ws-r.txt", "a b\n");
    DiffView dv;
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 1);

    DiffView::Options opts;
    opts.ignoreWhitespace = true;
    dv.setOptions(opts);
    QCOMPARE(dv.differenceCount(), 0);
}

void TestDiffView::ignore_blank_lines_option() {
    const QString l = writeFile("blank-l.txt", "a\n\nb\n");
    const QString r = writeFile("blank-r.txt", "a\nb\n");
    DiffView dv;
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 1);

    DiffView::Options opts;
    opts.ignoreBlankLines = true;
    dv.setOptions(opts);
    QCOMPARE(dv.differenceCount(), 0);

    // setOptions with no files loaded is harmless.
    DiffView fresh;
    fresh.setOptions(opts);
    QCOMPARE(fresh.differenceCount(), 0);
}

void TestDiffView::editing_pane_sets_dirty_and_save_writes() {
    const QString l = writeFile("edit-l.txt", "leftfirst\ncommon\n");
    const QString r = writeFile("edit-r.txt", "rightfirst\ncommon\n");
    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QVERIFY(dv.setFiles(l, r));

    DiffPane* left = paneContaining(dv, "leftfirst");
    QVERIFY(left);
    QSignalSpy dirtySpy(&dv, &DiffView::dirtyChanged);

    // Edit at the start of the "leftfirst" line (row 0 may be an alignment
    // filler, which is not part of the file).
    QTextBlock blk = left->document()->firstBlock();
    while (blk.isValid() && blk.text() != "leftfirst") blk = blk.next();
    QVERIFY(blk.isValid());
    left->setTextCursor(QTextCursor(blk));
    left->textCursor().insertText("Z");
    QVERIFY(dv.isDirty());
    QCOMPARE(dirtySpy.count(), 1);
    QCOMPARE(dirtySpy.last().at(0).toBool(), true);

    QVERIFY(dv.save());
    QVERIFY(!dv.isDirty());
    QCOMPARE(dirtySpy.count(), 2);
    QCOMPARE(readFile(l), QByteArray("Zleftfirst\ncommon\n"));
}

void TestDiffView::arrow_copy_and_undo() {
    const QString l = writeFile("arrow-l.txt", "a\nLEFT\nc\n");
    const QString r = writeFile("arrow-r.txt", "a\nRIGHT\nc\n");
    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 1);

    DiffPane* left = paneContaining(dv, "LEFT");
    QVERIFY(left);

    // Clicking the arrow on the block (row 1) copies left over right.
    QVERIFY(QMetaObject::invokeMethod(left, "arrowClicked", Q_ARG(int, 1)));
    QCOMPARE(dv.differenceCount(), 0);
    QVERIFY(dv.isDirty());
    QVERIFY(dv.canUndoArrow());

    dv.undoArrow();
    QCOMPARE(dv.differenceCount(), 1);
    QVERIFY(!dv.canUndoArrow());
    QVERIFY(dv.isDirty());

    // save() persists and clears the arrow-undo stack.
    QVERIFY(QMetaObject::invokeMethod(left, "arrowClicked", Q_ARG(int, 1)));
    QVERIFY(dv.canUndoArrow());
    QVERIFY(dv.save());
    QVERIFY(!dv.canUndoArrow());
    QCOMPARE(readFile(r), QByteArray("a\nLEFT\nc\n"));
}

void TestDiffView::partial_row_copy() {
    const QString l = writeFile("part-l.txt", "ctx\nred apple\ngreen pear\ntail\n");
    const QString r = writeFile("part-r.txt", "ctx\nred orange\ngreen plum\ntail\n");
    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 1);

    DiffPane* left = paneContaining(dv, "red apple");
    QVERIFY(left);

    // Select rows 1-2 via click + shift-click, then arrow-copy: the picked
    // lines are inserted on the right without deleting anything.
    QVERIFY(QMetaObject::invokeMethod(left, "lineNumberClicked",
                                      Q_ARG(int, 1), Q_ARG(bool, false)));
    QVERIFY(QMetaObject::invokeMethod(left, "lineNumberClicked",
                                      Q_ARG(int, 2), Q_ARG(bool, true)));
    QVERIFY(QMetaObject::invokeMethod(left, "arrowClicked", Q_ARG(int, 1)));
    QVERIFY(dv.isDirty());
    QVERIFY(dv.save());
    QCOMPARE(readFile(r),
             QByteArray("ctx\nred apple\ngreen pear\nred orange\ngreen plum\ntail\n"));
}

void TestDiffView::partial_lone_row_click_toggles() {
    const QString l = writeFile("tog-l.txt", "ctx\nred apple\ngreen pear\ntail\n");
    const QString r = writeFile("tog-r.txt", "ctx\nred orange\ngreen plum\ntail\n");
    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QVERIFY(dv.setFiles(l, r));

    DiffPane* left = paneContaining(dv, "red apple");
    QVERIFY(left);
    auto bgStyle = [&](int row) {
        return left->document()->findBlockByNumber(row).blockFormat().background().style();
    };
    auto bgColor = [&](int row) {
        return left->document()->findBlockByNumber(row).blockFormat().background().color();
    };

    QVERIFY(QMetaObject::invokeMethod(left, "lineNumberClicked",
                                      Q_ARG(int, 1), Q_ARG(bool, false)));
    QCOMPARE(bgColor(1), QColor(200, 220, 255));   // selected
    QCOMPARE(bgStyle(2), Qt::NoBrush);             // neutral sibling

    // Clicking the same lone row again clears the selection.
    QVERIFY(QMetaObject::invokeMethod(left, "lineNumberClicked",
                                      Q_ARG(int, 1), Q_ARG(bool, false)));
    QVERIFY(bgColor(1) != QColor(200, 220, 255));

    // A click outside any block clears too (row 0 is an Equal row).
    QVERIFY(QMetaObject::invokeMethod(left, "lineNumberClicked",
                                      Q_ARG(int, 1), Q_ARG(bool, false)));
    QVERIFY(QMetaObject::invokeMethod(left, "lineNumberClicked",
                                      Q_ARG(int, 0), Q_ARG(bool, false)));
    QVERIFY(bgColor(1) != QColor(200, 220, 255));
}

void TestDiffView::search_bar_interactions() {
    const QString l = writeFile("find-l.txt", "needle one\nplain\nneedle two\n");
    const QString r = writeFile("find-r.txt", "needle one\nplain\nneedle two\n");
    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QVERIFY(dv.setFiles(l, r));

    dv.showSearchBar();
    auto* edit = dv.findChild<QLineEdit*>();
    QVERIFY(edit);
    QVERIFY(edit->isVisible());

    QTest::keyClicks(edit, "needle");
    auto panes = dv.findChildren<DiffPane*>();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes[0]->matchCount(), 2);
    QCOMPARE(panes[1]->matchCount(), 2);

    // A status label reports counts for both sides.
    auto hasLabelContaining = [&](const QString& s) {
        for (auto* lbl : dv.findChildren<QLabel*>()) {
            if (lbl->text().contains(s)) return true;
        }
        return false;
    };
    QVERIFY(hasLabelContaining("(L:2 R:2)"));

    // Enter advances, Shift+Enter goes back (left pane is the target).
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(hasLabelContaining("1/2"));
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(hasLabelContaining("2/2"));
    QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
    QVERIFY(hasLabelContaining("1/2"));

    // The ◀ / ▶ buttons drive the same navigation. (The line edit's clear
    // button is also a QToolButton, so select the nav buttons by label.)
    auto buttonByText = [&](const QString& text) -> QToolButton* {
        for (auto* b : dv.findChildren<QToolButton*>()) {
            if (b->text() == text) return b;
        }
        return nullptr;
    };
    QToolButton* prev = buttonByText("◀");
    QToolButton* next = buttonByText("▶");
    QToolButton* close = buttonByText("✕");
    QVERIFY(prev && next && close);
    next->click();
    QVERIFY(hasLabelContaining("2/2"));
    prev->click();
    QVERIFY(hasLabelContaining("1/2"));

    // No matches.
    edit->setText("zzz-nothing");
    QVERIFY(hasLabelContaining("no matches"));

    // Close button hides the bar and clears the term.
    close->click();
    QVERIFY(!edit->isVisible());
    QCOMPARE(panes[0]->matchCount(), 0);

    // Escape from the edit closes the bar too.
    dv.showSearchBar();
    QTest::keyClicks(edit, "plain");
    QVERIFY(panes[0]->matchCount() > 0);
    QTest::keyClick(edit, Qt::Key_Escape);
    QVERIFY(!edit->isVisible());
    QCOMPARE(panes[0]->matchCount(), 0);
}

void TestDiffView::font_size_adjustments_propagate() {
    const int original = DiffView::diffFontPt();

    // Host the view in a QMainWindow so the zoom status message code runs.
    QMainWindow mw;
    auto* dv = new DiffView;
    mw.setCentralWidget(dv);
    mw.resize(800, 600);
    mw.show();

    const QString l = writeFile("font-l.txt", "a\n");
    QVERIFY(dv->setFiles(l, l));

    DiffView::adjustDiffFontPt(+1);
    QCOMPARE(DiffView::diffFontPt(), original + 1);
    auto panes = dv->findChildren<DiffPane*>();
    QCOMPARE(panes[0]->font().pointSize(), original + 1);

    // Clamped at both ends.
    DiffView::setDiffFontPt(999);
    QCOMPARE(DiffView::diffFontPt(), 32);
    DiffView::setDiffFontPt(1);
    QCOMPARE(DiffView::diffFontPt(), 6);

    DiffView::setDiffFontPt(original);
    QCOMPARE(DiffView::diffFontPt(), original);
}

void TestDiffView::splitter_state_round_trip() {
    DiffView dv;
    const QByteArray state = dv.saveSplitterState();
    QVERIFY(!state.isEmpty());
    dv.restoreSplitterState(state);
    QCOMPARE(dv.saveSplitterState(), state);
}

void TestDiffView::truncated_file_loads_more() {
    // Files above the 10 MB threshold stream in 10,000-line batches.
    QByteArray big;
    const QByteArray pad(1100, 'x');
    big.reserve(11000 * (pad.size() + 8));
    for (int i = 0; i < 11000; ++i) {
        big += pad + QString(" %1\n").arg(i).toUtf8();
    }
    QVERIFY(big.size() > 10 * 1024 * 1024);
    const QString l = writeFile("big-l.txt", big);
    const QString r = writeFile("big-r.txt", "x\n");

    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QSignalSpy spy(&dv, &DiffView::currentDifferenceChanged);
    QVERIFY(dv.setFiles(l, r));
    QVERIFY(dv.isAnyFileTruncated());

    // The path label announces the truncation.
    bool truncatedLabel = false;
    for (auto* lbl : dv.findChildren<QLabel*>()) {
        if (lbl->text().contains("TRUNCATED")) truncatedLabel = true;
    }
    QVERIFY(truncatedLabel);

    // The marker rows sit at the bottom of the panes.
    DiffPane* left = paneContaining(dv, "x 0");
    QVERIFY(left);
    QVERIFY(left->document()->blockCount() > 10000);

    // Ctrl+N with the cursor on the marker loads the remaining lines.
    left->moveCursor(QTextCursor::End);
    QVERIFY(left->isCursorOnTruncationMarker());
    spy.clear();
    dv.nextDifference();
    QVERIFY(!dv.isAnyFileTruncated());
    QVERIFY(left->document()->blockCount() >= 11000);
    QVERIFY(spy.count() >= 1);
}

void TestDiffView::external_file_change_reloads() {
    const QString l = writeFile("watch-l.txt", "a\nb\n");
    const QString r = writeFile("watch-r.txt", "a\nb\n");
    DiffView dv;
    dv.resize(800, 600);
    dv.show();
    QVERIFY(dv.setFiles(l, r));
    QCOMPARE(dv.differenceCount(), 0);

    // Rewrite the left file behind the view's back; the file watcher
    // reloads it and the diff appears.
    writeFile("watch-l.txt", "a\nCHANGED b\n");
    QTRY_COMPARE_WITH_TIMEOUT(dv.differenceCount(), 1, 5000);
}

int main(int argc, char* argv[]) {
    // Keep QSettings (diff font size) out of the user's real config.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication::setAttribute(Qt::AA_Use96Dpi, true);
    QApplication app(argc, argv);
    TestDiffView tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_diffview.moc"
