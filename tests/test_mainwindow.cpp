#include <QtTest/QtTest>

#include <QAction>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>

#include "DiffPane.h"
#include "DiffView.h"
#include "ImageDiffView.h"
#include "MainWindow.h"
#include "TreeCompareView.h"

// NOTE: the modal paths (file dialogs, About/difftool boxes, dirty-close
// prompts) are intentionally not exercised — they would block the offscreen
// event loop.
class TestMainWindow : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString writeFile(const QString& rel, const QByteArray& content) {
        const QString path = m_dir.path() + "/" + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(content);
        return path;
    }

    QString makePng(const QString& rel) {
        const QString path = m_dir.path() + "/" + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QImage img(20, 20, QImage::Format_RGB32);
        img.fill(Qt::magenta);
        img.save(path, "PNG");
        return path;
    }

    // A folder pair under `prefix` with one same and two different files.
    QPair<QString, QString> makeTreeFixture(const QString& prefix) {
        writeFile(prefix + "/left/same.txt", "same\n");
        writeFile(prefix + "/left/diff.txt", "left version\n");
        writeFile(prefix + "/left/sub/inner.txt", "left inner\n");
        writeFile(prefix + "/right/same.txt", "same\n");
        writeFile(prefix + "/right/diff.txt", "right version\n");
        writeFile(prefix + "/right/sub/inner.txt", "right inner\n");
        return {m_dir.path() + "/" + prefix + "/left",
                m_dir.path() + "/" + prefix + "/right"};
    }

    static QAction* actionByText(MainWindow& mw, const QString& text) {
        for (auto* a : mw.findChildren<QAction*>()) {
            if (a->text() == text) return a;
        }
        return nullptr;
    }

    static QMenu* menuByTitle(MainWindow& mw, const QString& title) {
        for (auto* m : mw.findChildren<QMenu*>()) {
            if (m->title() == title) return m;
        }
        return nullptr;
    }

    static bool statusContains(MainWindow& mw, const QString& s) {
        for (auto* lbl : mw.statusBar()->findChildren<QLabel*>()) {
            if (lbl->text().contains(s)) return true;
        }
        return false;
    }

    static DiffPane* paneContaining(QWidget* root, const QString& needle) {
        for (auto* p : root->findChildren<DiffPane*>()) {
            if (p->toPlainText().contains(needle)) return p;
        }
        return nullptr;
    }

private slots:
    void recent_menus_empty_initially();
    void empty_window_state();
    void load_pair_creates_and_reuses_diff_tab();
    void load_image_pair_creates_image_tab();
    void load_folder_pair_creates_tree_tab();
    void diff_tab_status_and_navigation();
    void tree_tab_status_and_filter_combo();
    void ignore_case_action_applies_to_open_tabs();
    void dirty_edit_enables_save_and_stars_title();
    void undo_action_and_ctrl_z_undo_arrow_copy();
    void next_different_file_cycles_tree_pairs();
    void closing_tabs_and_last_tab_closes_window();
    void recent_menus_populate_and_reload();
    void extract_if_archive_handles_tar();
    void load_from_cli_routes_files_and_archives();
    void close_event_persists_settings();
    void tab_cycling_shortcuts();
};

void TestMainWindow::recent_menus_empty_initially() {
    QSettings().clear();
    MainWindow mw;
    for (const QString& title : {QString("Recent &Pairs"), QString("Recent F&older Pairs")}) {
        QMenu* menu = menuByTitle(mw, title);
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QCOMPARE(menu->actions().size(), 1);
        QCOMPARE(menu->actions().first()->text(), QString("(none)"));
        QVERIFY(!menu->actions().first()->isEnabled());
    }
}

void TestMainWindow::empty_window_state() {
    MainWindow mw;
    QCOMPARE(mw.windowTitle(), QString("twain"));
    QVERIFY(!actionByText(mw, "&Save")->isEnabled());
    QVERIFY(!actionByText(mw, "&Next Difference")->isEnabled());
    QVERIFY(!actionByText(mw, "&Previous Difference")->isEnabled());
    QVERIFY(!actionByText(mw, "&Refresh")->isEnabled());
}

void TestMainWindow::load_pair_creates_and_reuses_diff_tab() {
    const QString l = writeFile("pair/left/same.txt", "a\n");
    const QString r = writeFile("pair/right/same.txt", "a\n");
    MainWindow mw;
    auto* tabs = mw.findChild<QTabWidget*>();
    QVERIFY(tabs);

    mw.loadPair(l, r);
    QCOMPARE(tabs->count(), 1);
    // Same basename on both sides: single-name title.
    QCOMPARE(tabs->tabText(0), QString("same.txt"));
    QVERIFY(tabs->tabToolTip(0).contains(l));
    QVERIFY(statusContains(mw, "No differences"));
    QVERIFY(mw.windowTitle().contains("same.txt"));

    // The same pair again reuses the open tab.
    mw.loadPair(l, r);
    QCOMPARE(tabs->count(), 1);

    // Distinct basenames produce a combined title.
    const QString other = writeFile("pair/other.txt", "b\n");
    mw.loadPair(l, other);
    QCOMPARE(tabs->count(), 2);
    QVERIFY(tabs->tabText(1).contains("⟷"));
}

void TestMainWindow::load_image_pair_creates_image_tab() {
    const QString l = makePng("img/left.png");
    const QString r = makePng("img/right.png");
    MainWindow mw;
    auto* tabs = mw.findChild<QTabWidget*>();

    mw.loadPair(l, r);
    QCOMPARE(tabs->count(), 1);
    QVERIFY(qobject_cast<ImageDiffView*>(tabs->currentWidget()));

    mw.loadPair(l, r);  // reuse
    QCOMPARE(tabs->count(), 1);
}

void TestMainWindow::load_folder_pair_creates_tree_tab() {
    const auto [l, r] = makeTreeFixture("folders");
    MainWindow mw;
    auto* tabs = mw.findChild<QTabWidget*>();

    mw.loadFolderPair(l, r);
    QCOMPARE(tabs->count(), 1);
    QVERIFY(qobject_cast<TreeCompareView*>(tabs->currentWidget()));
    QVERIFY(tabs->tabText(0).endsWith("/"));
    QVERIFY(mw.windowTitle().contains("left/"));

    mw.loadFolderPair(l, r);  // reuse
    QCOMPARE(tabs->count(), 1);
}

void TestMainWindow::diff_tab_status_and_navigation() {
    const QString l = writeFile("nav/l.txt", "same0\nalpha one\nsame2\nbeta one\nsame4\n");
    const QString r = writeFile("nav/r.txt", "same0\nalpha two\nsame2\nbeta two\nsame4\n");
    MainWindow mw;
    mw.resize(1000, 700);
    mw.show();
    mw.loadPair(l, r);

    QVERIFY(statusContains(mw, "2 differences"));
    QVERIFY(actionByText(mw, "&Next Difference")->isEnabled());
    QVERIFY(actionByText(mw, "&Refresh")->isEnabled());

    actionByText(mw, "&Next Difference")->trigger();
    QVERIFY(statusContains(mw, "Diff 1 / 2"));
    actionByText(mw, "&Next Difference")->trigger();
    QVERIFY(statusContains(mw, "Diff 2 / 2"));
    actionByText(mw, "&Previous Difference")->trigger();
    QVERIFY(statusContains(mw, "Diff 1 / 2"));

    // Refresh re-reads the files and keeps the diff count.
    actionByText(mw, "&Refresh")->trigger();
    QVERIFY(statusContains(mw, "differences"));
}

void TestMainWindow::tree_tab_status_and_filter_combo() {
    const auto [l, r] = makeTreeFixture("treestatus");
    MainWindow mw;
    mw.loadFolderPair(l, r);

    QVERIFY(statusContains(mw, "1 same"));
    QVERIFY(statusContains(mw, "2 different"));

    auto* tv = qobject_cast<TreeCompareView*>(mw.findChild<QTabWidget*>()->currentWidget());
    QVERIFY(tv);
    // New tree tabs default to DifferencesOnly (from settings).
    QCOMPARE(tv->filter(), TreeCompareModel::FilterMode::DifferencesOnly);

    // Switching the toolbar combo re-filters the current tree.
    auto* combo = mw.findChild<QComboBox*>();
    QVERIFY(combo);
    combo->setCurrentIndex(0);  // "All"
    QCOMPARE(tv->filter(), TreeCompareModel::FilterMode::All);

    // Refresh on a tree tab re-runs the comparison.
    actionByText(mw, "&Refresh")->trigger();
    QVERIFY(statusContains(mw, "1 same"));
}

void TestMainWindow::ignore_case_action_applies_to_open_tabs() {
    const QString l = writeFile("case/l.txt", "HELLO World\n");
    const QString r = writeFile("case/r.txt", "hello world\n");
    MainWindow mw;
    mw.loadPair(l, r);
    auto* dv = qobject_cast<DiffView*>(mw.findChild<QTabWidget*>()->currentWidget());
    QVERIFY(dv);
    QCOMPARE(dv->differenceCount(), 1);

    QAction* ignoreCase = actionByText(mw, "Ignore &Case");
    ignoreCase->setChecked(true);
    QCOMPARE(dv->differenceCount(), 0);
    QVERIFY(statusContains(mw, "No differences"));
    ignoreCase->setChecked(false);
    QCOMPARE(dv->differenceCount(), 1);
}

void TestMainWindow::dirty_edit_enables_save_and_stars_title() {
    const QString l = writeFile("dirty/l.txt", "leftfirst\ncommon\n");
    const QString r = writeFile("dirty/r.txt", "rightfirst\ncommon\n");
    MainWindow mw;
    mw.resize(1000, 700);
    mw.show();
    mw.loadPair(l, r);
    auto* tabs = mw.findChild<QTabWidget*>();
    auto* dv = qobject_cast<DiffView*>(tabs->currentWidget());
    QVERIFY(dv);

    DiffPane* left = paneContaining(dv, "leftfirst");
    QVERIFY(left);
    QTextBlock blk = left->document()->firstBlock();
    while (blk.isValid() && blk.text() != "leftfirst") blk = blk.next();
    left->setTextCursor(QTextCursor(blk));
    left->textCursor().insertText("Z");

    QVERIFY(dv->isDirty());
    QVERIFY(tabs->tabText(tabs->currentIndex()).endsWith(" *"));
    QVERIFY(actionByText(mw, "&Save")->isEnabled());

    actionByText(mw, "&Save")->trigger();
    QVERIFY(!dv->isDirty());
    QVERIFY(!tabs->tabText(tabs->currentIndex()).endsWith(" *"));
    QFile f(l);
    f.open(QIODevice::ReadOnly);
    QVERIFY(f.readAll().startsWith("Zleftfirst\n"));
}

void TestMainWindow::undo_action_and_ctrl_z_undo_arrow_copy() {
    const QString l = writeFile("undo/l.txt", "a\nLEFT\nc\n");
    const QString r = writeFile("undo/r.txt", "a\nRIGHT\nc\n");
    MainWindow mw;
    mw.resize(1000, 700);
    mw.show();
    mw.loadPair(l, r);
    auto* dv = qobject_cast<DiffView*>(mw.findChild<QTabWidget*>()->currentWidget());
    QVERIFY(dv);
    DiffPane* left = paneContaining(dv, "LEFT");
    QVERIFY(left);

    // Arrow-copy, then undo through the menu action.
    QVERIFY(QMetaObject::invokeMethod(left, "arrowClicked", Q_ARG(int, 1)));
    QCOMPARE(dv->differenceCount(), 0);
    actionByText(mw, "&Undo")->trigger();
    QCOMPARE(dv->differenceCount(), 1);

    // Same again, undone via the app-wide Ctrl+Z shortcut-override filter.
    QVERIFY(QMetaObject::invokeMethod(left, "arrowClicked", Q_ARG(int, 1)));
    QCOMPARE(dv->differenceCount(), 0);
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Z, Qt::ControlModifier);
    QApplication::sendEvent(&mw, &override);
    QCOMPARE(dv->differenceCount(), 1);
}

void TestMainWindow::next_different_file_cycles_tree_pairs() {
    const auto [l, r] = makeTreeFixture("cycle");
    MainWindow mw;
    mw.resize(1000, 700);
    mw.show();
    mw.loadFolderPair(l, r);
    auto* tabs = mw.findChild<QTabWidget*>();
    QCOMPARE(tabs->count(), 1);

    QAction* nextFile = actionByText(mw, "Next Differing &File");

    // From the tree tab: opens a diff tab on the first differing file
    // (sub/inner.txt comes first in doc order).
    nextFile->trigger();
    QCOMPARE(tabs->count(), 2);
    auto* dv = qobject_cast<DiffView*>(tabs->currentWidget());
    QVERIFY(dv);
    QCOMPARE(dv->leftPath(), l + "/sub/inner.txt");

    // From the diff tab: advances to the next pair in the same tab.
    nextFile->trigger();
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(dv->leftPath(), l + "/diff.txt");

    // Exhausted: the diff tab closes and the tree tab remains.
    nextFile->trigger();
    QCOMPARE(tabs->count(), 1);
    QVERIFY(qobject_cast<TreeCompareView*>(tabs->currentWidget()));
}

void TestMainWindow::closing_tabs_and_last_tab_closes_window() {
    const QString a = writeFile("close/a.txt", "a\n");
    const QString b = writeFile("close/b.txt", "b\n");
    const QString c = writeFile("close/c.txt", "c\n");
    MainWindow mw;
    mw.show();
    mw.loadPair(a, b);
    mw.loadPair(a, c);
    auto* tabs = mw.findChild<QTabWidget*>();
    QCOMPARE(tabs->count(), 2);

    QAction* closeTab = actionByText(mw, "&Close Tab");
    closeTab->trigger();
    QCOMPARE(tabs->count(), 1);
    QVERIFY(mw.isVisible());

    // Closing the last tab closes the window.
    closeTab->trigger();
    QCOMPARE(tabs->count(), 0);
    QVERIFY(!mw.isVisible());
}

void TestMainWindow::recent_menus_populate_and_reload() {
    QSettings().clear();
    const QString l = writeFile("recent/left/log.txt", "x\n");
    const QString r = writeFile("recent/right/log.txt", "y\n");
    const auto [dl, dr] = makeTreeFixture("recentdirs");
    MainWindow mw;
    auto* tabs = mw.findChild<QTabWidget*>();

    mw.loadPair(l, r);
    mw.loadPair(l, r);  // duplicate is deduplicated in the recent list
    mw.loadFolderPair(dl, dr);

    QMenu* recent = menuByTitle(mw, "Recent &Pairs");
    QVERIFY(QMetaObject::invokeMethod(recent, "aboutToShow"));
    QCOMPARE(recent->actions().size(), 1);
    QCOMPARE(recent->actions().first()->text(), QString("log.txt ⟷ log.txt"));

    QMenu* recentFolders = menuByTitle(mw, "Recent F&older Pairs");
    QVERIFY(QMetaObject::invokeMethod(recentFolders, "aboutToShow"));
    QCOMPARE(recentFolders->actions().size(), 1);

    // Triggering a recent entry opens (here: re-focuses) the pair.
    tabs->setCurrentIndex(tabs->indexOf(
        qobject_cast<QWidget*>(mw.findChild<TreeCompareView*>())));
    recent->actions().first()->trigger();
    auto* dv = qobject_cast<DiffView*>(tabs->currentWidget());
    QVERIFY(dv);
    QCOMPARE(dv->leftPath(), l);
}

void TestMainWindow::extract_if_archive_handles_tar() {
    const QString plain = writeFile("arch/plain.txt", "p\n");
    writeFile("arch/src/file.txt", "in archive\n");
    const QString tarPath = m_dir.path() + "/arch/bundle.tar";
    QProcess tar;
    tar.start("tar", {"-cf", tarPath, "-C", m_dir.path() + "/arch/src", "."});
    QVERIFY(tar.waitForFinished(10000));
    QCOMPARE(tar.exitCode(), 0);

    MainWindow mw;
    // Non-archives pass through unchanged.
    QCOMPARE(mw.extractIfArchive(plain), plain);
    // Archives extract into a temp dir owned by the window.
    const QString extracted = mw.extractIfArchive(tarPath);
    QVERIFY(extracted != tarPath);
    QVERIFY(QFile::exists(extracted + "/file.txt"));
}

void TestMainWindow::load_from_cli_routes_files_and_archives() {
    const QString l = writeFile("cli/l.txt", "a\n");
    const QString r = writeFile("cli/r.txt", "b\n");
    const auto [dl, dr] = makeTreeFixture("clidirs");

    // Tar up both folder sides.
    auto makeTar = [&](const QString& src, const QString& name) {
        const QString tarPath = m_dir.path() + "/cli/" + name;
        QProcess tar;
        tar.start("tar", {"-cf", tarPath, "-C", src, "."});
        tar.waitForFinished(10000);
        return tarPath;
    };
    const QString tarL = makeTar(dl, "l.tar");
    const QString tarR = makeTar(dr, "r.tar");

    MainWindow mw;
    auto* tabs = mw.findChild<QTabWidget*>();

    mw.loadFromCli(l, r);  // plain files → diff tab
    QVERIFY(qobject_cast<DiffView*>(tabs->currentWidget()));

    mw.loadFromCli(tarL, tarR);  // archives of dirs → tree tab
    QVERIFY(qobject_cast<TreeCompareView*>(tabs->currentWidget()));
    QVERIFY(statusContains(mw, "2 different"));
}

void TestMainWindow::close_event_persists_settings() {
    QSettings().clear();
    const auto [l, r] = makeTreeFixture("persist");
    MainWindow mw;
    mw.show();
    mw.loadFolderPair(l, r);
    mw.close();

    QSettings s;
    QVERIFY(s.contains("geometry"));
    QVERIFY(s.contains("windowState"));
    QVERIFY(s.contains("tree/filter"));
    QVERIFY(s.contains("diff/ignoreCase"));
}

void TestMainWindow::tab_cycling_shortcuts() {
    const QString a = writeFile("cycle2/a.txt", "a\n");
    const QString b = writeFile("cycle2/b.txt", "b\n");
    const QString c = writeFile("cycle2/c.txt", "c\n");
    MainWindow mw;
    mw.show();
    mw.loadPair(a, b);
    mw.loadPair(a, c);
    auto* tabs = mw.findChild<QTabWidget*>();
    QCOMPARE(tabs->currentIndex(), 1);

    QTest::keyClick(&mw, Qt::Key_PageDown, Qt::ControlModifier);
    QCOMPARE(tabs->currentIndex(), 0);
    QTest::keyClick(&mw, Qt::Key_PageUp, Qt::ControlModifier);
    QCOMPARE(tabs->currentIndex(), 1);

    // Ctrl+Shift+PageUp moves the current tab left.
    const QString movedTitle = tabs->tabText(1);
    QTest::keyClick(&mw, Qt::Key_PageUp, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(tabs->tabText(0), movedTitle);
    QCOMPARE(tabs->currentIndex(), 0);

    // And back to the right.
    QTest::keyClick(&mw, Qt::Key_PageDown, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(tabs->tabText(1), movedTitle);
}

int main(int argc, char* argv[]) {
    // Keep QSettings (recents, geometry, diff options) out of the user's
    // real config.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication::setAttribute(Qt::AA_Use96Dpi, true);
    QApplication app(argc, argv);
    TestMainWindow tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_mainwindow.moc"
