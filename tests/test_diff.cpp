#include <QtTest/QtTest>

#include "Diff.h"

namespace {

QStringList L(std::initializer_list<const char*> lines) {
    QStringList out;
    for (auto* s : lines) out.append(QString::fromUtf8(s));
    return out;
}

// Reduce a hunks vector to a "shape" string for compact assertions:
//   "=N" for Equal of N lines on both sides
//   "-N" for Delete of N left lines
//   "+N" for Insert of N right lines
QString shape(const QVector<Diff::Hunk>& hunks) {
    QString out;
    for (const auto& h : hunks) {
        switch (h.op) {
            case Diff::Op::Equal:  out += QString("=%1").arg(h.leftCount); break;
            case Diff::Op::Delete: out += QString("-%1").arg(h.leftCount); break;
            case Diff::Op::Insert: out += QString("+%1").arg(h.rightCount); break;
        }
    }
    return out;
}

}  // namespace

class TestDiff : public QObject {
    Q_OBJECT
private slots:
    void identical_files_produce_one_equal_hunk();
    void disjoint_files_produce_delete_then_insert();
    void shared_prefix_and_suffix_are_trimmed();
    void after_is_suffix_of_before_aligns_correctly();
    void patience_anchors_align_subset_in_middle();
    void ignore_case_treats_lines_as_equal();
    void ignore_whitespace_treats_lines_as_equal();
    void empty_left_gives_pure_insert();
    void empty_right_gives_pure_delete();
    void both_empty_returns_no_hunks();
    void alignBlock_pairs_similar_lines();
    void alignBlock_coarse_fallback_above_cap();
    void lineDiff_marks_inserted_token();

    void hunk_indices_track_original_positions();
    void multiple_change_blocks_produce_separate_hunks();
    void mid_file_insert_yields_one_insert_hunk();
    void mid_file_delete_yields_one_delete_hunk();
    void ignoreCase_and_ignoreWhitespace_combine();
    void patience_drops_anchors_that_violate_ordering();
    void duplicated_line_is_not_used_as_anchor();
    void recursive_trim_aligns_shared_lines_after_anchor();
    void alignBlock_empty_left_returns_unpaired_right();
    void alignBlock_empty_right_returns_unpaired_left();
    void alignBlock_unrelated_lines_stay_unpaired();
    void lineDiff_identical_inputs_have_no_diff_segments();
    void lineDiff_common_prefix_only();
    void lineDiff_common_suffix_only();
    void similarity_identical_is_one();
    void similarity_disjoint_is_zero();
    void similarity_partial_overlap();
};

void TestDiff::identical_files_produce_one_equal_hunk() {
    const auto lines = L({"a", "b", "c"});
    const auto hunks = Diff::compute(lines, lines);
    QCOMPARE(shape(hunks), QString("=3"));
}

void TestDiff::disjoint_files_produce_delete_then_insert() {
    const auto left = L({"a", "b", "c"});
    const auto right = L({"x", "y", "z"});
    const auto hunks = Diff::compute(left, right);
    // Order can be Delete+Insert or Insert+Delete depending on Myers'
    // tiebreak; we only require both sides are fully covered.
    int del = 0, ins = 0;
    for (const auto& h : hunks) {
        if (h.op == Diff::Op::Delete) del += h.leftCount;
        if (h.op == Diff::Op::Insert) ins += h.rightCount;
    }
    QCOMPARE(del, 3);
    QCOMPARE(ins, 3);
}

void TestDiff::shared_prefix_and_suffix_are_trimmed() {
    const auto left  = L({"head1", "head2", "X", "tail1", "tail2"});
    const auto right = L({"head1", "head2", "Y", "tail1", "tail2"});
    const auto hunks = Diff::compute(left, right);
    // Expect: Equal(2 head) + one Delete + one Insert + Equal(2 tail).
    QCOMPARE(shape(hunks), QString("=2-1+1=2"));
}

void TestDiff::after_is_suffix_of_before_aligns_correctly() {
    // Simulates the trails.json shape: a chunk was removed from the start,
    // the rest is shared verbatim.
    const auto left  = L({"removed1", "removed2", "removed3",
                          "keep1", "keep2", "keep3"});
    const auto right = L({"keep1", "keep2", "keep3"});
    const auto hunks = Diff::compute(left, right);
    QCOMPARE(shape(hunks), QString("-3=3"));
}

void TestDiff::patience_anchors_align_subset_in_middle() {
    // 200 unique "removed" lines, then a unique "anchor" line, then 100
    // more removed lines. Right is just the anchor.
    // Without patience anchoring the absDiff would trip kMaxD and produce
    // a coarse delete-all/insert-all.
    QStringList left;
    for (int i = 0; i < 6000; ++i) left.append(QString("removed-pre-%1").arg(i));
    left.append("UNIQUE_ANCHOR");
    for (int i = 0; i < 100; ++i) left.append(QString("removed-post-%1").arg(i));
    QStringList right = L({"UNIQUE_ANCHOR"});

    const auto hunks = Diff::compute(left, right);

    // The anchor line should appear as an Equal hunk somewhere in the result.
    bool foundAnchor = false;
    for (const auto& h : hunks) {
        if (h.op == Diff::Op::Equal && h.leftCount == 1 &&
            h.leftStart == 6000 && h.rightStart == 0) {
            foundAnchor = true;
            break;
        }
    }
    QVERIFY2(foundAnchor, "patience should pin UNIQUE_ANCHOR as an Equal hunk");
}

void TestDiff::ignore_case_treats_lines_as_equal() {
    const auto left = L({"Hello", "World"});
    const auto right = L({"hello", "WORLD"});
    Diff::Options opts;
    opts.ignoreCase = true;
    QCOMPARE(shape(Diff::compute(left, right, opts)), QString("=2"));
    // Without the option they're all different.
    opts.ignoreCase = false;
    const auto hunks = Diff::compute(left, right, opts);
    int equals = 0;
    for (const auto& h : hunks) if (h.op == Diff::Op::Equal) equals += h.leftCount;
    QCOMPARE(equals, 0);
}

void TestDiff::ignore_whitespace_treats_lines_as_equal() {
    const auto left = L({"  hello  world  "});
    const auto right = L({"hello world"});
    Diff::Options opts;
    opts.ignoreWhitespace = true;
    QCOMPARE(shape(Diff::compute(left, right, opts)), QString("=1"));
}

void TestDiff::empty_left_gives_pure_insert() {
    const auto hunks = Diff::compute(QStringList{}, L({"a", "b"}));
    QCOMPARE(shape(hunks), QString("+2"));
}

void TestDiff::empty_right_gives_pure_delete() {
    const auto hunks = Diff::compute(L({"a", "b"}), QStringList{});
    QCOMPARE(shape(hunks), QString("-2"));
}

void TestDiff::both_empty_returns_no_hunks() {
    const auto hunks = Diff::compute(QStringList{}, QStringList{});
    QCOMPARE(hunks.size(), 0);
}

void TestDiff::alignBlock_pairs_similar_lines() {
    // Two lines on each side, each pair shares most words → alignBlock
    // should pair index 0<->0 and 1<->1.
    const auto left  = L({"apple banana cherry", "dog elephant fox"});
    const auto right = L({"apple banana date",   "dog elephant goat"});
    const auto pairs = Diff::alignBlock(left, right);
    // Expect every output pair to have both sides set (no unpaired rows).
    int paired = 0;
    for (const auto& p : pairs) {
        if (p.leftIdx >= 0 && p.rightIdx >= 0) ++paired;
    }
    QCOMPARE(paired, 2);
}

void TestDiff::alignBlock_coarse_fallback_above_cap() {
    // Build n*m just over the 1M-cell cap; alignBlock should return the
    // positional 1-to-1 fallback (no DP allocation, no similarity calls).
    const long long cap = Diff::kMaxAlignCells;
    const int n = static_cast<int>(cap / 500) + 1;  // ~2001 if cap is 1e6
    const int m = 501;
    QVERIFY(static_cast<long long>(n) * m > cap);
    QStringList left, right;
    for (int i = 0; i < n; ++i) left.append(QString("l-%1").arg(i));
    for (int i = 0; i < m; ++i) right.append(QString("r-%1").arg(i));
    const auto pairs = Diff::alignBlock(left, right);
    // Coarse fallback: min(n,m) paired k<->k, then leftovers unpaired.
    QCOMPARE(pairs.size(), n + m - qMin(n, m));
    for (int k = 0; k < qMin(n, m); ++k) {
        QCOMPARE(pairs[k].leftIdx, k);
        QCOMPARE(pairs[k].rightIdx, k);
    }
}

void TestDiff::lineDiff_marks_inserted_token() {
    // Token "world" exists only on the right.
    const auto ld = Diff::lineDiff("hello", "hello world");
    bool rightHasDiffSegment = false;
    for (const auto& seg : ld.right) {
        if (seg.differ) { rightHasDiffSegment = true; break; }
    }
    QVERIFY2(rightHasDiffSegment, "right side should mark the new ' world' segment as differing");
}

void TestDiff::hunk_indices_track_original_positions() {
    const auto left  = L({"a", "b", "X", "c"});
    const auto right = L({"a", "b", "Y", "c"});
    const auto hunks = Diff::compute(left, right);
    // Expect: Equal(2 starting at 0,0) + Delete(1 at left 2) + Insert(1 at right 2) + Equal(1 at 3,3).
    // (Or with Insert and Delete swapped.)
    QCOMPARE(hunks.size(), 4);
    QCOMPARE(hunks[0].op, Diff::Op::Equal);
    QCOMPARE(hunks[0].leftStart, 0);
    QCOMPARE(hunks[0].rightStart, 0);
    QCOMPARE(hunks[0].leftCount, 2);
    QCOMPARE(hunks[0].rightCount, 2);
    QCOMPARE(hunks.last().op, Diff::Op::Equal);
    QCOMPARE(hunks.last().leftStart, 3);
    QCOMPARE(hunks.last().rightStart, 3);
    QCOMPARE(hunks.last().leftCount, 1);
    QCOMPARE(hunks.last().rightCount, 1);
    // Middle hunks (in either order) cover the change at index 2 on each side.
    bool sawDelete = false, sawInsert = false;
    for (int i = 1; i <= 2; ++i) {
        const auto& h = hunks[i];
        if (h.op == Diff::Op::Delete) {
            QCOMPARE(h.leftStart, 2);
            QCOMPARE(h.leftCount, 1);
            sawDelete = true;
        } else if (h.op == Diff::Op::Insert) {
            QCOMPARE(h.rightStart, 2);
            QCOMPARE(h.rightCount, 1);
            sawInsert = true;
        }
    }
    QVERIFY(sawDelete && sawInsert);
}

void TestDiff::multiple_change_blocks_produce_separate_hunks() {
    const auto left  = L({"a", "X1", "b", "c", "Y1", "d"});
    const auto right = L({"a", "X2", "b", "c", "Y2", "d"});
    const auto hunks = Diff::compute(left, right);
    int equals = 0, deletes = 0, inserts = 0;
    for (const auto& h : hunks) {
        if (h.op == Diff::Op::Equal) equals += h.leftCount;
        if (h.op == Diff::Op::Delete) deletes += h.leftCount;
        if (h.op == Diff::Op::Insert) inserts += h.rightCount;
    }
    QCOMPARE(equals, 4);   // a, b, c, d
    QCOMPARE(deletes, 2);  // X1, Y1
    QCOMPARE(inserts, 2);  // X2, Y2
    // Two distinct change regions, separated by at least one Equal hunk.
    int changeBlocks = 0;
    bool inChange = false;
    for (const auto& h : hunks) {
        const bool isChange = (h.op != Diff::Op::Equal);
        if (isChange && !inChange) ++changeBlocks;
        inChange = isChange;
    }
    QCOMPARE(changeBlocks, 2);
}

void TestDiff::mid_file_insert_yields_one_insert_hunk() {
    const auto left  = L({"a", "b", "c"});
    const auto right = L({"a", "X", "Y", "b", "c"});
    QCOMPARE(shape(Diff::compute(left, right)), QString("=1+2=2"));
}

void TestDiff::mid_file_delete_yields_one_delete_hunk() {
    const auto left  = L({"a", "X", "Y", "b", "c"});
    const auto right = L({"a", "b", "c"});
    QCOMPARE(shape(Diff::compute(left, right)), QString("=1-2=2"));
}

void TestDiff::ignoreCase_and_ignoreWhitespace_combine() {
    const auto left  = L({"  Hello  WORLD  "});
    const auto right = L({"hello world"});
    Diff::Options opts;
    opts.ignoreCase = true;
    opts.ignoreWhitespace = true;
    QCOMPARE(shape(Diff::compute(left, right, opts)), QString("=1"));
    // Either option alone is not enough.
    Diff::Options onlyCase;       onlyCase.ignoreCase = true;
    Diff::Options onlyWs;         onlyWs.ignoreWhitespace = true;
    QVERIFY(shape(Diff::compute(left, right, onlyCase)) != QString("=1"));
    QVERIFY(shape(Diff::compute(left, right, onlyWs))   != QString("=1"));
}

void TestDiff::patience_drops_anchors_that_violate_ordering() {
    // A and B are both unique in each side but their order is swapped, so
    // patience's LIS can keep only one. The other line falls to Delete/Insert.
    const auto left  = L({"A", "B"});
    const auto right = L({"B", "A"});
    const auto hunks = Diff::compute(left, right);
    int equals = 0, deletes = 0, inserts = 0;
    for (const auto& h : hunks) {
        if (h.op == Diff::Op::Equal) equals += h.leftCount;
        if (h.op == Diff::Op::Delete) deletes += h.leftCount;
        if (h.op == Diff::Op::Insert) inserts += h.rightCount;
    }
    QCOMPARE(equals, 1);
    QCOMPARE(deletes, 1);
    QCOMPARE(inserts, 1);
}

void TestDiff::duplicated_line_is_not_used_as_anchor() {
    // "foo" appears twice in left so it can't be a patience anchor — but the
    // unique "X" and "Y" can. Result: the two foos should be a Delete chunk.
    const auto left  = L({"X", "foo", "foo", "Y"});
    const auto right = L({"X", "Y"});
    QCOMPARE(shape(Diff::compute(left, right)), QString("=1-2=1"));
}

void TestDiff::recursive_trim_aligns_shared_lines_after_anchor() {
    // Regression: a patience anchor (ANCHOR1) is followed by lines that
    // are shared but not anchor-eligible because they're duplicated on
    // the left ("shared_a", "shared_b"), followed by enough unique left
    // lines to make absDiff > kMaxD (5000). Without the recursive
    // prefix-trim inside diffNormalized, the between-anchor chunk would
    // hit Myers' coarseScript fallback and the two shared_* lines would
    // be marked Delete/Insert instead of Equal — matching the trails.json
    // bug. With recursion, those lines get trimmed as a shared prefix of
    // the chunk and appear as Equal.
    QStringList left;
    left.append("shared_a");
    left.append("ANCHOR1");
    left.append("shared_a");
    left.append("shared_b");
    for (int i = 0; i < 5001; ++i) left.append(QString("x-%1").arg(i));
    left.append("ANCHOR2");
    left.append("shared_b");
    const QStringList right = L({"ANCHOR1", "shared_a", "shared_b", "ANCHOR2"});
    // With the fix: leading shared_a is deleted; ANCHOR1+shared_a+shared_b
    // are Equal (3 lines); 5001 x-lines deleted; ANCHOR2 Equal; trailing
    // shared_b deleted.
    QCOMPARE(shape(Diff::compute(left, right)), QString("-1=3-5001=1-1"));
}

void TestDiff::alignBlock_empty_left_returns_unpaired_right() {
    const auto pairs = Diff::alignBlock(QStringList{}, L({"r1", "r2"}));
    QCOMPARE(pairs.size(), 2);
    for (const auto& p : pairs) QCOMPARE(p.leftIdx, -1);
}

void TestDiff::alignBlock_empty_right_returns_unpaired_left() {
    const auto pairs = Diff::alignBlock(L({"l1", "l2"}), QStringList{});
    QCOMPARE(pairs.size(), 2);
    for (const auto& p : pairs) QCOMPARE(p.rightIdx, -1);
}

void TestDiff::alignBlock_unrelated_lines_stay_unpaired() {
    // Below the 0.1 similarity threshold → no pairings.
    const auto left  = L({"alpha beta gamma"});
    const auto right = L({"xyz pdq qrs"});
    const auto pairs = Diff::alignBlock(left, right);
    for (const auto& p : pairs) {
        QVERIFY(p.leftIdx == -1 || p.rightIdx == -1);
    }
}

void TestDiff::lineDiff_identical_inputs_have_no_diff_segments() {
    const auto ld = Diff::lineDiff("hello world", "hello world");
    for (const auto& seg : ld.left)  QVERIFY(!seg.differ);
    for (const auto& seg : ld.right) QVERIFY(!seg.differ);
}

void TestDiff::lineDiff_common_prefix_only() {
    // Shared "hello " at the start; the rest differs on each side.
    const auto ld = Diff::lineDiff("hello world", "hello there");
    bool leftHasDiffSeg = false, rightHasDiffSeg = false;
    for (const auto& seg : ld.left)  if (seg.differ) leftHasDiffSeg = true;
    for (const auto& seg : ld.right) if (seg.differ) rightHasDiffSeg = true;
    QVERIFY(leftHasDiffSeg && rightHasDiffSeg);
    // The common prefix should also show up as a non-differing segment on each side.
    bool leftHasEqualSeg = false, rightHasEqualSeg = false;
    for (const auto& seg : ld.left)  if (!seg.differ) leftHasEqualSeg = true;
    for (const auto& seg : ld.right) if (!seg.differ) rightHasEqualSeg = true;
    QVERIFY(leftHasEqualSeg && rightHasEqualSeg);
}

void TestDiff::lineDiff_common_suffix_only() {
    // Shared "world" at the end.
    const auto ld = Diff::lineDiff("hello world", "goodbye world");
    bool leftHasEqualSeg = false, rightHasEqualSeg = false;
    for (const auto& seg : ld.left)  if (!seg.differ) leftHasEqualSeg = true;
    for (const auto& seg : ld.right) if (!seg.differ) rightHasEqualSeg = true;
    QVERIFY(leftHasEqualSeg && rightHasEqualSeg);
}

void TestDiff::similarity_identical_is_one() {
    QCOMPARE(Diff::similarity("foo bar baz", "foo bar baz"), 1.0);
}

void TestDiff::similarity_disjoint_is_zero() {
    QCOMPARE(Diff::similarity("aaa bbb", "ccc ddd"), 0.0);
}

void TestDiff::similarity_partial_overlap() {
    // Jaccard("foo bar" {foo,bar}, "foo qux" {foo,qux}) = 1 / 3.
    const double s = Diff::similarity("foo bar", "foo qux");
    QVERIFY(s > 0.30 && s < 0.34);
}

QTEST_GUILESS_MAIN(TestDiff)
#include "test_diff.moc"
