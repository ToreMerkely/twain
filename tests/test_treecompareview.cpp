#include <QtTest/QtTest>

#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeView>
#include <QWheelEvent>

#include "DiffView.h"
#include "TreeCompareModel.h"
#include "TreeCompareView.h"

class TestTreeCompareView : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString makePath(const QString& rel) {
        const QString path = m_dir.path() + "/" + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        return path;
    }

    void writeFile(const QString& rel, const QByteArray& content) {
        QFile f(makePath(rel));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(content);
    }

    // Standard fixture: 1 same, 2 different (one nested), 1 left-only,
    // 1 right-only. `prefix` keeps each test's tree isolated.
    QPair<QString, QString> makeFixture(const QString& prefix) {
        writeFile(prefix + "/left/same.txt", "same\n");
        writeFile(prefix + "/left/diff.txt", "left version\n");
        writeFile(prefix + "/left/only-left.txt", "l\n");
        writeFile(prefix + "/left/sub/inner.txt", "left inner\n");
        writeFile(prefix + "/right/same.txt", "same\n");
        writeFile(prefix + "/right/diff.txt", "right version\n");
        writeFile(prefix + "/right/only-right.txt", "r\n");
        writeFile(prefix + "/right/sub/inner.txt", "right inner\n");
        return {m_dir.path() + "/" + prefix + "/left",
                m_dir.path() + "/" + prefix + "/right"};
    }

    // The left tree is the one showing the left-name column; the right tree
    // hides it.
    static QTreeView* leftTree(TreeCompareView& v) {
        for (auto* t : v.findChildren<QTreeView*>()) {
            if (!t->isColumnHidden(TreeCompareModel::kColLeftName)) return t;
        }
        return nullptr;
    }
    static QTreeView* rightTree(TreeCompareView& v) {
        for (auto* t : v.findChildren<QTreeView*>()) {
            if (t->isColumnHidden(TreeCompareModel::kColLeftName)) return t;
        }
        return nullptr;
    }

private slots:
    void set_folders_counts_leaves();
    void empty_folders_compare_clean();
    void next_prev_different_file_navigation();
    void next_different_file_emits_activation_when_open();
    void filter_round_trip();
    void expand_collapse_sync_between_trees();
    void scroll_sync_between_trees();
    void enter_key_activates_file_and_toggles_dir();
    void double_click_activates_file_and_toggles_dir();
    void header_and_splitter_state_round_trip();
    void font_size_applies_and_ctrl_wheel_zooms();
    void search_bar_finds_names_and_wraps();

};

void TestTreeCompareView::set_folders_counts_leaves() {
    const auto [l, r] = makeFixture("counts");
    TreeCompareView v;
    QSignalSpy spy(&v, &TreeCompareView::comparisonUpdated);
    QVERIFY(v.setFolders(l, r));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(v.sameCount(), 1);
    QCOMPARE(v.differentCount(), 2);  // diff.txt + sub/inner.txt
    QCOMPARE(v.leftOnlyCount(), 1);
    QCOMPARE(v.rightOnlyCount(), 1);
}

void TestTreeCompareView::empty_folders_compare_clean() {
    QDir().mkpath(m_dir.path() + "/empty/left");
    QDir().mkpath(m_dir.path() + "/empty/right");
    TreeCompareView v;
    QVERIFY(v.setFolders(m_dir.path() + "/empty/left", m_dir.path() + "/empty/right"));
    QCOMPARE(v.sameCount(), 0);
    QCOMPARE(v.differentCount(), 0);
    QCOMPARE(v.nextDifferentFile(false), qMakePair(QString(), QString()));
    QCOMPARE(v.prevDifferentFile(false), qMakePair(QString(), QString()));
}

void TestTreeCompareView::next_prev_different_file_navigation() {
    const auto [l, r] = makeFixture("nav");
    TreeCompareView v;
    v.resize(800, 600);
    v.show();
    QVERIFY(v.setFolders(l, r));

    // Doc order with dirs first: sub/inner.txt, diff.txt, only-left.txt,
    // only-right.txt; same.txt is skipped and the walk stops at the end.
    auto next = [&]() { return v.nextDifferentFile(/*open=*/false); };
    QCOMPARE(next(), qMakePair(l + "/sub/inner.txt", r + "/sub/inner.txt"));
    QCOMPARE(next(), qMakePair(l + "/diff.txt", r + "/diff.txt"));
    QCOMPARE(next(), qMakePair(l + "/only-left.txt", QString()));
    QCOMPARE(next(), qMakePair(QString(), r + "/only-right.txt"));
    QCOMPARE(next(), qMakePair(QString(), QString()));

    // And back up again (the cursor is on only-right.txt).
    auto prev = [&]() { return v.prevDifferentFile(/*open=*/false); };
    QCOMPARE(prev(), qMakePair(l + "/only-left.txt", QString()));
    QCOMPARE(prev(), qMakePair(l + "/diff.txt", r + "/diff.txt"));
    QCOMPARE(prev(), qMakePair(l + "/sub/inner.txt", r + "/sub/inner.txt"));
    QCOMPARE(prev(), qMakePair(QString(), QString()));
}

void TestTreeCompareView::next_different_file_emits_activation_when_open() {
    const auto [l, r] = makeFixture("open");
    TreeCompareView v;
    QVERIFY(v.setFolders(l, r));

    QSignalSpy spy(&v, &TreeCompareView::fileActivated);
    v.nextDifferentFile(/*open=*/true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toString(), l + "/sub/inner.txt");
    QCOMPARE(spy.last().at(1).toString(), r + "/sub/inner.txt");

    spy.clear();
    v.nextDifferentFile(/*open=*/false);
    QCOMPARE(spy.count(), 0);
}

void TestTreeCompareView::filter_round_trip() {
    const auto [l, r] = makeFixture("filter");
    TreeCompareView v;
    QVERIFY(v.setFolders(l, r));
    QCOMPARE(v.filter(), TreeCompareModel::FilterMode::All);
    v.setFilter(TreeCompareModel::FilterMode::DifferencesOnly);
    QCOMPARE(v.filter(), TreeCompareModel::FilterMode::DifferencesOnly);
    // Counts are filter-independent (they describe the comparison).
    QCOMPARE(v.sameCount(), 1);
}

void TestTreeCompareView::expand_collapse_sync_between_trees() {
    const auto [l, r] = makeFixture("sync");
    TreeCompareView v;
    v.resize(800, 600);
    v.show();
    QVERIFY(v.setFolders(l, r));

    QTreeView* lt = leftTree(v);
    QTreeView* rt = rightTree(v);
    QVERIFY(lt && rt);
    const QModelIndex sub = lt->model()->index(0, 0);  // "sub" sorts first
    QVERIFY(sub.isValid());

    // setFolders expand-alls the left tree, mirrored to the right.
    QVERIFY(lt->isExpanded(sub));
    QVERIFY(rt->isExpanded(sub));

    lt->collapse(sub);
    QVERIFY(!rt->isExpanded(sub));
    rt->expand(sub);
    QVERIFY(lt->isExpanded(sub));
    rt->collapse(sub);
    QVERIFY(!lt->isExpanded(sub));
}

void TestTreeCompareView::scroll_sync_between_trees() {
    for (int i = 0; i < 60; ++i) {
        const QByteArray body = QString("file %1\n").arg(i).toUtf8();
        writeFile(QString("scroll/left/f%1.txt").arg(i, 2, 10, QChar('0')), body);
        writeFile(QString("scroll/right/f%1.txt").arg(i, 2, 10, QChar('0')), body);
    }
    TreeCompareView v;
    v.resize(500, 150);
    v.show();
    QVERIFY(v.setFolders(m_dir.path() + "/scroll/left", m_dir.path() + "/scroll/right"));

    QTreeView* lt = leftTree(v);
    QTreeView* rt = rightTree(v);
    QVERIFY(lt->verticalScrollBar()->maximum() > 0);
    lt->verticalScrollBar()->setValue(5);
    QCOMPARE(rt->verticalScrollBar()->value(), 5);
    rt->verticalScrollBar()->setValue(2);
    QCOMPARE(lt->verticalScrollBar()->value(), 2);
}

void TestTreeCompareView::enter_key_activates_file_and_toggles_dir() {
    const auto [l, r] = makeFixture("enter");
    TreeCompareView v;
    v.resize(800, 600);
    v.show();
    QVERIFY(v.setFolders(l, r));

    QTreeView* lt = leftTree(v);
    const QModelIndex sub = lt->model()->index(0, 0);
    const QModelIndex diffFile = lt->model()->index(1, 0);  // diff.txt

    QSignalSpy spy(&v, &TreeCompareView::fileActivated);
    lt->setCurrentIndex(diffFile);
    QTest::keyClick(lt, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toString(), l + "/diff.txt");

    // Return on a directory toggles expansion instead.
    lt->setCurrentIndex(sub);
    QVERIFY(lt->isExpanded(sub));
    QTest::keyClick(lt, Qt::Key_Return);
    QVERIFY(!lt->isExpanded(sub));
    QTest::keyClick(lt, Qt::Key_Enter);
    QVERIFY(lt->isExpanded(sub));
    QCOMPARE(spy.count(), 1);
}

void TestTreeCompareView::double_click_activates_file_and_toggles_dir() {
    const auto [l, r] = makeFixture("dclick");
    TreeCompareView v;
    v.resize(800, 600);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    QVERIFY(v.setFolders(l, r));

    QTreeView* lt = leftTree(v);
    const QModelIndex sub = lt->model()->index(0, 0);
    const QModelIndex diffFile = lt->model()->index(1, 0);

    // A double-click only registers on the index that received the
    // preceding press, so click first like a real user would.
    auto doubleClick = [&](const QModelIndex& idx) {
        lt->scrollTo(idx);
        const QPoint pos = lt->visualRect(idx).center();
        QTest::mouseClick(lt->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
        QTest::mouseDClick(lt->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    };

    QSignalSpy spy(&v, &TreeCompareView::fileActivated);
    doubleClick(diffFile);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).toString(), l + "/diff.txt");

    // Double-click on a directory toggles it.
    QVERIFY(lt->isExpanded(sub));
    doubleClick(sub);
    QVERIFY(!lt->isExpanded(sub));
    QCOMPARE(spy.count(), 1);
}

void TestTreeCompareView::header_and_splitter_state_round_trip() {
    const auto [l, r] = makeFixture("state");
    TreeCompareView v;
    QVERIFY(v.setFolders(l, r));

    const QByteArray splitter = v.saveSplitterState();
    QVERIFY(!splitter.isEmpty());
    v.restoreSplitterState(splitter);
    QCOMPARE(v.saveSplitterState(), splitter);

    const QByteArray header = v.saveHeaderState();
    QVERIFY(!header.isEmpty());
    v.restoreHeaderState(header);
    // restoreHeaderState re-applies the fixed resize modes.
    QCOMPARE(leftTree(v)->header()->sectionResizeMode(TreeCompareModel::kColLeftName),
             QHeaderView::Stretch);
    QCOMPARE(rightTree(v)->header()->sectionResizeMode(TreeCompareModel::kColRightSize),
             QHeaderView::ResizeToContents);
}

void TestTreeCompareView::font_size_applies_and_ctrl_wheel_zooms() {
    const int originalPt = DiffView::diffFontPt();
    const auto [l, r] = makeFixture("font");
    TreeCompareView v;
    v.resize(800, 600);
    v.show();
    QVERIFY(v.setFolders(l, r));
    v.focusTree();

    QTreeView* lt = leftTree(v);
    QCOMPARE(lt->font().pointSize(), originalPt);

    // Ctrl+wheel on a tree bumps the app-wide diff font size.
    QWheelEvent ev(QPointF(30, 30), QPointF(lt->viewport()->mapToGlobal(QPoint(30, 30))),
                   QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(lt->viewport(), &ev);
    QCOMPARE(DiffView::diffFontPt(), originalPt + 1);

    v.applyDiffFontSize();
    QCOMPARE(lt->font().pointSize(), originalPt + 1);
    DiffView::setDiffFontPt(originalPt);
}

int main(int argc, char* argv[]) {
    // Keep QSettings (diff font size) out of the user's real config.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication::setAttribute(Qt::AA_Use96Dpi, true);
    QApplication app(argc, argv);
    TestTreeCompareView tc;
    return QTest::qExec(&tc, argc, argv);
}

void TestTreeCompareView::search_bar_finds_names_and_wraps() {
    const auto [l, r] = makeFixture("search");
    TreeCompareView v;
    v.setFolders(l, r);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    auto* edit = v.findChild<QLineEdit*>();
    QVERIFY(edit);
    QVERIFY(!edit->isVisible());
    auto* tree = leftTree(v);
    QVERIFY(tree);
    // Right-only entries have an empty left-name column.
    auto nameAt = [&]() {
        const QModelIndex cur = tree->currentIndex();
        const QString l = cur.siblingAtColumn(TreeCompareModel::kColLeftName).data().toString();
        return l.isEmpty()
            ? cur.siblingAtColumn(TreeCompareModel::kColRightName).data().toString()
            : l;
    };
    QLabel* status = nullptr;
    for (auto* lb : v.findChildren<QLabel*>()) {
        if (lb->text().isEmpty()) status = lb;
    }
    QVERIFY(status);

    v.showSearchBar();
    QVERIFY(edit->isVisible());
    QVERIFY(edit->hasFocus());

    // Typing jumps to the first match from the current position; case-insensitive.
    edit->setText("INNER");
    QCOMPARE(nameAt(), QString("inner.txt"));
    QCOMPARE(status->text(), QString("1/1"));

    // Two matches for "only"; next/prev cycle and wrap.
    edit->setText("only");
    QCOMPARE(nameAt(), QString("only-left.txt"));
    QCOMPARE(status->text(), QString("1/2"));
    QVERIFY(v.findNext());
    QCOMPARE(nameAt(), QString("only-right.txt"));
    QCOMPARE(status->text(), QString("2/2"));
    QVERIFY(v.findNext());
    QCOMPARE(nameAt(), QString("only-left.txt"));
    QVERIFY(v.findPrev());
    QCOMPARE(nameAt(), QString("only-right.txt"));

    // Directory names match too.
    edit->setText("sub");
    QCOMPARE(nameAt(), QString("sub"));

    edit->setText("nope");
    QCOMPARE(status->text(), QString("no matches"));
    QVERIFY(!v.findNext());

    // Esc clears, hides, and returns focus to the tree.
    QTest::keyClick(edit, Qt::Key_Escape);
    QVERIFY(!edit->isVisible());
    QVERIFY(edit->text().isEmpty());
    QVERIFY(tree->hasFocus());
}

#include "test_treecompareview.moc"
