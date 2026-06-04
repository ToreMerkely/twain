#include <QtTest/QtTest>

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QIcon>

#include "TreeCompare.h"
#include "TreeCompareModel.h"

namespace {

using TreeCompare::Entry;
using TreeCompare::Status;

// Build a leaf entry that exists on both sides (paths drive the model's
// "this side has an entry" logic, so non-empty paths mean the column shows).
Entry bothSidesFile(const QString& name, Status status, qint64 size = 10) {
    Entry e;
    e.name = name;
    e.isDir = false;
    e.status = status;
    e.leftPath = "/l/" + name;
    e.rightPath = "/r/" + name;
    e.leftSize = size;
    e.rightSize = size;
    e.leftMTime = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5));
    e.rightMTime = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5));
    return e;
}

Entry leftOnlyFile(const QString& name, qint64 size = 10) {
    Entry e;
    e.name = name;
    e.isDir = false;
    e.status = Status::LeftOnly;
    e.leftPath = "/l/" + name;
    e.leftSize = size;
    e.leftMTime = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5));
    return e;
}

Entry rightOnlyFile(const QString& name, qint64 size = 10) {
    Entry e;
    e.name = name;
    e.isDir = false;
    e.status = Status::RightOnly;
    e.rightPath = "/r/" + name;
    e.rightSize = size;
    e.rightMTime = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5));
    return e;
}

Entry dir(const QString& name, Status status, QVector<Entry> children) {
    Entry e;
    e.name = name;
    e.isDir = true;
    e.status = status;
    e.leftPath = "/l/" + name;
    e.rightPath = "/r/" + name;
    e.leftSize = -1;
    e.rightSize = -1;
    e.children = std::move(children);
    return e;
}

Entry root(QVector<Entry> children) {
    Entry e;
    e.isDir = true;
    e.leftPath = "/l";
    e.rightPath = "/r";
    e.children = std::move(children);
    return e;
}

}  // namespace

class TestTreeCompareModel : public QObject {
    Q_OBJECT
private slots:
    void column_count_is_fixed();
    void header_labels_for_both_sides();
    void header_ignores_vertical_and_non_display_roles();
    void row_count_and_index_navigation();
    void parent_round_trips_through_index();
    void index_rejects_out_of_range_and_bad_columns();
    void entry_for_invalid_index_is_root();
    void display_role_shows_name_size_mtime();
    void single_sided_entry_blanks_the_missing_side();
    void foreground_role_colors_by_status();
    void filter_differences_only_hides_same_files();
    void filter_left_only_keeps_only_left_entries();
    void filter_right_only_keeps_only_right_entries();
    void empty_folder_is_hidden();
    void decoration_role_shows_icons_only_on_present_name_columns();
    void font_and_unknown_roles_are_unset();
    void right_only_entry_colors_right_side_blue();
    void notify_deep_compare_done_resets_when_root_populated();
    void destruction_through_base_pointer();
};

void TestTreeCompareModel::column_count_is_fixed() {
    TreeCompareModel m;
    QCOMPARE(m.columnCount(QModelIndex()), TreeCompareModel::kColCount);
    QCOMPARE(TreeCompareModel::kColCount, 6);
}

void TestTreeCompareModel::header_labels_for_both_sides() {
    TreeCompareModel m;
    auto h = [&](int s) {
        return m.headerData(s, Qt::Horizontal, Qt::DisplayRole).toString();
    };
    QCOMPARE(h(TreeCompareModel::kColLeftName), QString("Name"));
    QCOMPARE(h(TreeCompareModel::kColLeftSize), QString("Size"));
    QCOMPARE(h(TreeCompareModel::kColLeftMTime), QString("Modified"));
    QCOMPARE(h(TreeCompareModel::kColRightName), QString("Name"));
    QCOMPARE(h(TreeCompareModel::kColRightSize), QString("Size"));
    QCOMPARE(h(TreeCompareModel::kColRightMTime), QString("Modified"));
}

void TestTreeCompareModel::header_ignores_vertical_and_non_display_roles() {
    TreeCompareModel m;
    QVERIFY(!m.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());
    QVERIFY(!m.headerData(0, Qt::Horizontal, Qt::DecorationRole).isValid());
    QVERIFY(!m.headerData(99, Qt::Horizontal, Qt::DisplayRole).isValid());
}

void TestTreeCompareModel::row_count_and_index_navigation() {
    TreeCompareModel m;
    m.setRoot(root({
        dir("d", Status::Different, {bothSidesFile("inner.txt", Status::Different)}),
        bothSidesFile("top.txt", Status::Same),
    }));

    // Two visible children at the top level.
    QCOMPARE(m.rowCount(QModelIndex()), 2);

    // The directory (sorted first) has one visible child.
    const QModelIndex dIdx = m.index(0, 0, QModelIndex());
    QVERIFY(dIdx.isValid());
    QCOMPARE(m.entryFor(dIdx)->name, QString("d"));
    QCOMPARE(m.rowCount(dIdx), 1);

    const QModelIndex innerIdx = m.index(0, 0, dIdx);
    QVERIFY(innerIdx.isValid());
    QCOMPARE(m.entryFor(innerIdx)->name, QString("inner.txt"));
    QCOMPARE(m.rowCount(innerIdx), 0);

    // rowCount on a non-zero column is zero (Qt tree convention).
    QCOMPARE(m.rowCount(m.index(0, 1, QModelIndex())), 0);
}

void TestTreeCompareModel::parent_round_trips_through_index() {
    TreeCompareModel m;
    m.setRoot(root({
        dir("d", Status::Different, {bothSidesFile("inner.txt", Status::Different)}),
    }));

    const QModelIndex dIdx = m.index(0, 0, QModelIndex());
    const QModelIndex innerIdx = m.index(0, 0, dIdx);
    QVERIFY(innerIdx.isValid());

    // parent(inner) == d, and top-level entries have an invalid parent.
    QCOMPARE(m.parent(innerIdx), dIdx);
    QVERIFY(!m.parent(dIdx).isValid());
    QVERIFY(!m.parent(QModelIndex()).isValid());
}

void TestTreeCompareModel::index_rejects_out_of_range_and_bad_columns() {
    TreeCompareModel m;
    m.setRoot(root({bothSidesFile("a.txt", Status::Same)}));

    QVERIFY(!m.index(0, -1, QModelIndex()).isValid());
    QVERIFY(!m.index(0, TreeCompareModel::kColCount, QModelIndex()).isValid());
    QVERIFY(!m.index(-1, 0, QModelIndex()).isValid());
    QVERIFY(!m.index(5, 0, QModelIndex()).isValid());  // beyond row count
}

void TestTreeCompareModel::entry_for_invalid_index_is_root() {
    TreeCompareModel m;
    m.setRoot(root({bothSidesFile("a.txt", Status::Same)}));
    // An invalid index maps to the (nameless) root entry.
    const Entry* e = m.entryFor(QModelIndex());
    QVERIFY(e);
    QVERIFY(e->name.isEmpty());
    QVERIFY(e->isDir);
}

void TestTreeCompareModel::display_role_shows_name_size_mtime() {
    TreeCompareModel m;
    m.setRoot(root({
        dir("d", Status::Different, {bothSidesFile("inner.txt", Status::Same)}),
        bothSidesFile("f.txt", Status::Same, 1234),
    }));

    const QModelIndex fIdx = m.index(1, 0, QModelIndex());  // f.txt sorts after dir
    QCOMPARE(m.entryFor(fIdx)->name, QString("f.txt"));

    auto disp = [&](int col) {
        return m.data(m.index(1, col, QModelIndex()), Qt::DisplayRole);
    };
    QCOMPARE(disp(TreeCompareModel::kColLeftName).toString(), QString("f.txt"));
    QCOMPARE(disp(TreeCompareModel::kColRightName).toString(), QString("f.txt"));
    QVERIFY(!disp(TreeCompareModel::kColLeftSize).toString().isEmpty());
    QVERIFY(!disp(TreeCompareModel::kColRightSize).toString().isEmpty());
    QCOMPARE(disp(TreeCompareModel::kColLeftMTime).toString(), QString("2026-01-02 03:04:05"));

    // Directories show no size string.
    const QModelIndex dIdx = m.index(0, 0, QModelIndex());
    QVERIFY(m.data(m.index(0, TreeCompareModel::kColLeftSize, QModelIndex()),
                   Qt::DisplayRole).toString().isEmpty());
    QCOMPARE(m.entryFor(dIdx)->name, QString("d"));

    // Invalid index yields an invalid variant.
    QVERIFY(!m.data(QModelIndex(), Qt::DisplayRole).isValid());
}

void TestTreeCompareModel::single_sided_entry_blanks_the_missing_side() {
    TreeCompareModel m;
    m.setRoot(root({leftOnlyFile("only-left.txt")}));

    const QModelIndex idx = m.index(0, 0, QModelIndex());
    QVERIFY(idx.isValid());
    // Left columns populate, right columns are blank because there is no
    // right-side entry.
    QCOMPARE(m.data(m.index(0, TreeCompareModel::kColLeftName, QModelIndex()),
                    Qt::DisplayRole).toString(), QString("only-left.txt"));
    QVERIFY(!m.data(m.index(0, TreeCompareModel::kColRightName, QModelIndex()),
                    Qt::DisplayRole).isValid());
}

void TestTreeCompareModel::foreground_role_colors_by_status() {
    TreeCompareModel m;
    m.setRoot(root({
        bothSidesFile("diff.txt", Status::Different),
        leftOnlyFile("only-left.txt"),
    }));

    // Different rows are red on both sides.
    const QVariant fgDiffL =
        m.data(m.index(0, TreeCompareModel::kColLeftName, QModelIndex()), Qt::ForegroundRole);
    QVERIFY(fgDiffL.canConvert<QBrush>());
    QCOMPARE(fgDiffL.value<QBrush>().color(), QColor(200, 0, 0));

    // Left-only is blue on the left, and the right side is blank (no brush).
    const QVariant fgLeft =
        m.data(m.index(1, TreeCompareModel::kColLeftName, QModelIndex()), Qt::ForegroundRole);
    QCOMPARE(fgLeft.value<QBrush>().color(), QColor(0, 80, 200));
    const QVariant fgRight =
        m.data(m.index(1, TreeCompareModel::kColRightName, QModelIndex()), Qt::ForegroundRole);
    QVERIFY(!fgRight.isValid());
}

void TestTreeCompareModel::filter_differences_only_hides_same_files() {
    TreeCompareModel m;
    m.setRoot(root({
        bothSidesFile("same.txt", Status::Same),
        bothSidesFile("diff.txt", Status::Different),
        dir("only-same", Status::Same, {bothSidesFile("x.txt", Status::Same)}),
    }));
    QCOMPARE(m.rowCount(QModelIndex()), 3);

    m.setFilter(TreeCompareModel::FilterMode::DifferencesOnly);
    // Only "diff.txt" survives; the all-same folder collapses away.
    QCOMPARE(m.rowCount(QModelIndex()), 1);
    QCOMPARE(m.entryFor(m.index(0, 0, QModelIndex()))->name, QString("diff.txt"));

    // setFilter to the same mode is a no-op (covers the early return).
    m.setFilter(TreeCompareModel::FilterMode::DifferencesOnly);
    QCOMPARE(m.rowCount(QModelIndex()), 1);
}

void TestTreeCompareModel::filter_left_only_keeps_only_left_entries() {
    TreeCompareModel m;
    m.setRoot(root({
        leftOnlyFile("l.txt"),
        rightOnlyFile("r.txt"),
        bothSidesFile("same.txt", Status::Same),
    }));

    m.setFilter(TreeCompareModel::FilterMode::LeftOnly);
    QCOMPARE(m.rowCount(QModelIndex()), 1);
    QCOMPARE(m.entryFor(m.index(0, 0, QModelIndex()))->name, QString("l.txt"));
}

void TestTreeCompareModel::filter_right_only_keeps_only_right_entries() {
    TreeCompareModel m;
    m.setRoot(root({
        leftOnlyFile("l.txt"),
        rightOnlyFile("r.txt"),
        bothSidesFile("same.txt", Status::Same),
    }));

    m.setFilter(TreeCompareModel::FilterMode::RightOnly);
    QCOMPARE(m.rowCount(QModelIndex()), 1);
    QCOMPARE(m.entryFor(m.index(0, 0, QModelIndex()))->name, QString("r.txt"));
}

void TestTreeCompareModel::empty_folder_is_hidden() {
    // A folder with no visible descendants is itself hidden.
    TreeCompareModel m;
    m.setRoot(root({
        dir("empty", Status::Same, {}),
        bothSidesFile("a.txt", Status::Same),
    }));
    QCOMPARE(m.rowCount(QModelIndex()), 1);
    QCOMPARE(m.entryFor(m.index(0, 0, QModelIndex()))->name, QString("a.txt"));
}

void TestTreeCompareModel::decoration_role_shows_icons_only_on_present_name_columns() {
    TreeCompareModel m;
    m.setRoot(root({
        dir("d", Status::Different, {bothSidesFile("inner.txt", Status::Same)}),
        leftOnlyFile("only-left.txt"),
    }));

    // Folder icon on the directory's name column, file icon on the file's.
    const QVariant dirIcon =
        m.data(m.index(0, TreeCompareModel::kColLeftName, QModelIndex()), Qt::DecorationRole);
    QVERIFY(dirIcon.canConvert<QIcon>());
    const QModelIndex dIdx = m.index(0, 0, QModelIndex());
    const QVariant fileIcon =
        m.data(m.index(0, TreeCompareModel::kColRightName, dIdx), Qt::DecorationRole);
    QVERIFY(fileIcon.canConvert<QIcon>());

    // No icon on non-name columns...
    QVERIFY(!m.data(m.index(0, TreeCompareModel::kColLeftSize, QModelIndex()),
                    Qt::DecorationRole).isValid());
    // ...and none on the side where the entry does not exist.
    QVERIFY(!m.data(m.index(1, TreeCompareModel::kColRightName, QModelIndex()),
                    Qt::DecorationRole).isValid());
}

void TestTreeCompareModel::font_and_unknown_roles_are_unset() {
    TreeCompareModel m;
    m.setRoot(root({bothSidesFile("a.txt", Status::Same)}));
    const QModelIndex idx = m.index(0, TreeCompareModel::kColLeftName, QModelIndex());
    QVERIFY(!m.data(idx, Qt::FontRole).isValid());
    QVERIFY(!m.data(idx, Qt::ToolTipRole).isValid());
    // A Same-status file gets no foreground color either.
    QVERIFY(!m.data(idx, Qt::ForegroundRole).isValid());
}

void TestTreeCompareModel::right_only_entry_colors_right_side_blue() {
    TreeCompareModel m;
    m.setRoot(root({rightOnlyFile("only-right.txt")}));

    const QVariant fgRight =
        m.data(m.index(0, TreeCompareModel::kColRightName, QModelIndex()), Qt::ForegroundRole);
    QVERIFY(fgRight.canConvert<QBrush>());
    QCOMPARE(fgRight.value<QBrush>().color(), QColor(0, 80, 200));
    // The left side has no entry, so no brush there.
    QVERIFY(!m.data(m.index(0, TreeCompareModel::kColLeftName, QModelIndex()),
                    Qt::ForegroundRole).isValid());
    // The right-side mtime column renders for a right-only entry.
    QCOMPARE(m.data(m.index(0, TreeCompareModel::kColRightMTime, QModelIndex()),
                    Qt::DisplayRole).toString(), QString("2026-01-02 03:04:05"));
}

void TestTreeCompareModel::notify_deep_compare_done_resets_when_root_populated() {
    TreeCompareModel m;
    QSignalSpy spy(&m, &QAbstractItemModel::modelReset);

    // Empty root: early return, no reset.
    m.notifyDeepCompareDone();
    QCOMPARE(spy.count(), 0);

    m.setRoot(root({bothSidesFile("a.txt", Status::Same)}));
    spy.clear();
    m.notifyDeepCompareDone();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.rowCount(QModelIndex()), 1);
}

void TestTreeCompareModel::destruction_through_base_pointer() {
    // Views own models polymorphically; deleting through the base class must
    // run TreeCompareModel's destructor (which frees the icon provider).
    QAbstractItemModel* m = new TreeCompareModel();
    QCOMPARE(m->columnCount(QModelIndex()), TreeCompareModel::kColCount);
    delete m;
}

QTEST_MAIN(TestTreeCompareModel)
#include "test_treecomparemodel.moc"
