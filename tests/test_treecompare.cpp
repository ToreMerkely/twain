#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TreeCompare.h"

namespace {

using TreeCompare::Entry;
using TreeCompare::Status;

// Write `content` to `dir/relPath`, creating intermediate directories as
// needed. relPath is interpreted as forward-slash-separated.
void writeFile(const QString& dir, const QString& relPath, const QByteArray& content = {}) {
    const QString fullPath = QDir(dir).filePath(relPath);
    QDir().mkpath(QFileInfo(fullPath).path());
    QFile f(fullPath);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(QString("could not open %1").arg(fullPath)));
    if (!content.isEmpty()) f.write(content);
}

void makeDir(const QString& dir, const QString& relPath) {
    QVERIFY(QDir(dir).mkpath(relPath));
}

const Entry* findEntry(const Entry& e, const QString& name) {
    for (const auto& c : e.children) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

// Recursively assert every entry in the subtree has status `s`.
void assertAll(const Entry& e, Status s) {
    QCOMPARE(e.status, s);
    for (const auto& c : e.children) assertAll(c, s);
}

}  // namespace

class TestTreeCompare : public QObject {
    Q_OBJECT
private slots:
    void identical_folders_compare_as_same();
    void left_only_and_right_only_files_are_classified();
    void same_name_different_content_is_different();
    void nested_directory_aggregates_to_different();
    void hidden_files_are_included();
    void file_vs_directory_same_name_is_different();
    void empty_folders_compare_as_same();
    void symlink_cycle_does_not_hang();
    void children_are_sorted_dirs_first_then_name();
};

void TestTreeCompare::identical_folders_compare_as_same() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), "a.txt", "hello");
    writeFile(l.path(), "sub/b.txt", "world");
    writeFile(r.path(), "a.txt", "hello");
    writeFile(r.path(), "sub/b.txt", "world");

    const auto root = TreeCompare::compare(l.path(), r.path());
    assertAll(root, Status::Same);
    QCOMPARE(root.children.size(), 2);  // a.txt + sub/
}

void TestTreeCompare::left_only_and_right_only_files_are_classified() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), "only-left.txt", "L");
    writeFile(r.path(), "only-right.txt", "R");

    const auto root = TreeCompare::compare(l.path(), r.path());
    QCOMPARE(root.status, Status::Different);

    const auto* L = findEntry(root, "only-left.txt");
    const auto* R = findEntry(root, "only-right.txt");
    QVERIFY(L);
    QVERIFY(R);
    QCOMPARE(L->status, Status::LeftOnly);
    QCOMPARE(R->status, Status::RightOnly);
}

void TestTreeCompare::same_name_different_content_is_different() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), "a.txt", "version A");
    writeFile(r.path(), "a.txt", "version B");

    const auto root = TreeCompare::compare(l.path(), r.path());
    const auto* a = findEntry(root, "a.txt");
    QVERIFY(a);
    QCOMPARE(a->status, Status::Different);
    QCOMPARE(root.status, Status::Different);
}

void TestTreeCompare::nested_directory_aggregates_to_different() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), "sub/inner/file.txt", "L");
    writeFile(r.path(), "sub/inner/file.txt", "R");

    const auto root = TreeCompare::compare(l.path(), r.path());
    QCOMPARE(root.status, Status::Different);
    const auto* sub = findEntry(root, "sub");
    QVERIFY(sub);
    QCOMPARE(sub->status, Status::Different);
    const auto* inner = findEntry(*sub, "inner");
    QVERIFY(inner);
    QCOMPARE(inner->status, Status::Different);
}

void TestTreeCompare::hidden_files_are_included() {
    // Regression: TreeCompare enumerates with QDir::Hidden so dot-files
    // appear in the result.
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), ".hidden", "same");
    writeFile(r.path(), ".hidden", "same");

    const auto root = TreeCompare::compare(l.path(), r.path());
    const auto* h = findEntry(root, ".hidden");
    QVERIFY2(h, "dot-files should appear in the compare result");
    QCOMPARE(h->status, Status::Same);
}

void TestTreeCompare::file_vs_directory_same_name_is_different() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), "x", "I am a file");
    makeDir(r.path(), "x");

    const auto root = TreeCompare::compare(l.path(), r.path());
    const auto* x = findEntry(root, "x");
    QVERIFY(x);
    QCOMPARE(x->status, Status::Different);
}

void TestTreeCompare::empty_folders_compare_as_same() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());

    const auto root = TreeCompare::compare(l.path(), r.path());
    QCOMPARE(root.status, Status::Same);
    QCOMPARE(root.children.size(), 0);
}

void TestTreeCompare::symlink_cycle_does_not_hang() {
    // Both sides have a "loop" symlink pointing back to themselves. The
    // compare() implementation tracks visited canonical paths to break
    // the cycle. This test should complete in well under a second; if the
    // guard is removed it would recurse forever.
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    writeFile(l.path(), "file.txt", "same");
    writeFile(r.path(), "file.txt", "same");
    QVERIFY(QFile::link(l.path(), QDir(l.path()).filePath("loop")));
    QVERIFY(QFile::link(r.path(), QDir(r.path()).filePath("loop")));

    QElapsedTimer t;
    t.start();
    const auto root = TreeCompare::compare(l.path(), r.path());
    QVERIFY2(t.elapsed() < 5000, "compare() did not terminate within 5s — cycle guard regressed");
    // The "loop" entry exists but its subtree was pruned by the visited-set
    // guard, so it should compare as Same with no expanded children.
    const auto* loop = findEntry(root, "loop");
    QVERIFY(loop);
    QCOMPARE(loop->status, Status::Same);
}

void TestTreeCompare::children_are_sorted_dirs_first_then_name() {
    QTemporaryDir l, r;
    QVERIFY(l.isValid() && r.isValid());
    // Create entries in non-alphabetical, non-grouped order on each side.
    writeFile(l.path(), "zfile",    "z"); writeFile(r.path(), "zfile",    "z");
    writeFile(l.path(), "afile",    "a"); writeFile(r.path(), "afile",    "a");
    makeDir(l.path(),   "mdir");          makeDir(r.path(),   "mdir");
    makeDir(l.path(),   "bdir");          makeDir(r.path(),   "bdir");

    const auto root = TreeCompare::compare(l.path(), r.path());
    QCOMPARE(root.children.size(), 4);
    // Directories first (alphabetical), then files (alphabetical).
    QCOMPARE(root.children[0].name, QString("bdir"));
    QVERIFY(root.children[0].isDir);
    QCOMPARE(root.children[1].name, QString("mdir"));
    QVERIFY(root.children[1].isDir);
    QCOMPARE(root.children[2].name, QString("afile"));
    QVERIFY(!root.children[2].isDir);
    QCOMPARE(root.children[3].name, QString("zfile"));
    QVERIFY(!root.children[3].isDir);
}

QTEST_GUILESS_MAIN(TestTreeCompare)
#include "test_treecompare.moc"
