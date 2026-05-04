#include "MainWindow.h"

#include "DiffView.h"
#include "TreeCompareView.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>

#include "TreeCompareModel.h"

namespace {
constexpr int kMaxRecent = 10;
QString recentKey(const QString& prefix, int i, bool left) {
    return QString("%1/%2/%3").arg(prefix).arg(i).arg(left ? "left" : "right");
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("twain");
    resize(1400, 900);

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    readSettings();
    updateForCurrentTab();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    writeSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::createActions() {
    auto icon = [this](QStyle::StandardPixmap sp) { return style()->standardIcon(sp); };

    m_actOpenPair = new QAction(icon(QStyle::SP_DialogOpenButton), "Open &Pair...", this);
    m_actOpenPair->setShortcut(QKeySequence::Open);
    connect(m_actOpenPair, &QAction::triggered, this, &MainWindow::openPair);

    m_actOpenLeft = new QAction("Open &Left...", this);
    connect(m_actOpenLeft, &QAction::triggered, this, &MainWindow::openLeft);

    m_actOpenRight = new QAction("Open &Right...", this);
    connect(m_actOpenRight, &QAction::triggered, this, &MainWindow::openRight);

    m_actOpenFolderPair = new QAction(icon(QStyle::SP_DirIcon), "Open &Folder Pair...", this);
    m_actOpenFolderPair->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(m_actOpenFolderPair, &QAction::triggered, this, &MainWindow::openFolderPair);

    m_actRefresh = new QAction(icon(QStyle::SP_BrowserReload), "&Refresh", this);
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    m_actRefresh->setEnabled(false);
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::refresh);

    m_actQuit = new QAction("&Quit", this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    m_actNextDiff = new QAction(icon(QStyle::SP_ArrowDown), "&Next Difference", this);
    m_actNextDiff->setShortcuts({Qt::Key_F7, QKeySequence(Qt::CTRL | Qt::Key_N)});
    m_actNextDiff->setEnabled(false);
    connect(m_actNextDiff, &QAction::triggered, this, &MainWindow::nextDifference);

    m_actPrevDiff = new QAction(icon(QStyle::SP_ArrowUp), "&Previous Difference", this);
    m_actPrevDiff->setShortcuts({QKeySequence(Qt::SHIFT | Qt::Key_F7), QKeySequence(Qt::CTRL | Qt::Key_P)});
    m_actPrevDiff->setEnabled(false);
    connect(m_actPrevDiff, &QAction::triggered, this, &MainWindow::prevDifference);

    m_actNextDiffFile = new QAction("Next Differing &File", this);
    m_actNextDiffFile->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(m_actNextDiffFile, &QAction::triggered, this, &MainWindow::nextDifferentFile);
    addAction(m_actNextDiffFile);  // not in toolbar; shortcut active globally

    m_actCloseTab = new QAction("&Close Tab", this);
    m_actCloseTab->setShortcut(QKeySequence::Close);  // Ctrl+W
    connect(m_actCloseTab, &QAction::triggered, this, [this]() {
        const int idx = m_tabs->currentIndex();
        if (idx >= 0) onTabCloseRequested(idx);
    });
    addAction(m_actCloseTab);

    auto makeToggle = [this](const QString& label) {
        auto* a = new QAction(label, this);
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this](bool) { applyDiffOptionsToAllTabs(); });
        return a;
    };
    m_actIgnoreCase = makeToggle("Ignore &Case");
    m_actIgnoreWhitespace = makeToggle("Ignore &Whitespace");
    m_actIgnoreBlankLines = makeToggle("Ignore &Blank Lines");

    m_actAbout = new QAction("&About", this);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_actOpenPair);
    fileMenu->addAction(m_actOpenLeft);
    fileMenu->addAction(m_actOpenRight);
    fileMenu->addAction(m_actOpenFolderPair);
    m_recentMenu = fileMenu->addMenu("Recent &Pairs");
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    m_recentFoldersMenu = fileMenu->addMenu("Recent F&older Pairs");
    connect(m_recentFoldersMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentFoldersMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actRefresh);
    fileMenu->addAction(m_actCloseTab);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_actNextDiff);
    viewMenu->addAction(m_actPrevDiff);
    viewMenu->addAction(m_actNextDiffFile);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actIgnoreCase);
    viewMenu->addAction(m_actIgnoreWhitespace);
    viewMenu->addAction(m_actIgnoreBlankLines);

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction(m_actAbout);
}

void MainWindow::createToolBar() {
    auto* tb = addToolBar("Main");
    tb->setObjectName("MainToolBar");
    tb->addAction(m_actOpenPair);
    tb->addAction(m_actOpenFolderPair);
    tb->addAction(m_actRefresh);
    tb->addSeparator();
    tb->addAction(m_actPrevDiff);
    tb->addAction(m_actNextDiff);
    tb->addSeparator();

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem("All", int(TreeCompareModel::FilterMode::All));
    m_filterCombo->addItem("Differences only", int(TreeCompareModel::FilterMode::DifferencesOnly));
    m_filterCombo->addItem("Left only", int(TreeCompareModel::FilterMode::LeftOnly));
    m_filterCombo->addItem("Right only", int(TreeCompareModel::FilterMode::RightOnly));
    m_filterCombo->setCurrentIndex(1);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (auto* t = currentTreeView()) {
                    t->setFilter(static_cast<TreeCompareModel::FilterMode>(
                        m_filterCombo->currentData().toInt()));
                }
            });
    m_filterComboAction = tb->addWidget(m_filterCombo);
}

void MainWindow::createStatusBar() {
    m_leftPathLabel = new QLabel(this);
    m_rightPathLabel = new QLabel(this);
    m_diffCountLabel = new QLabel(this);
    statusBar()->addWidget(m_leftPathLabel, 1);
    statusBar()->addWidget(m_rightPathLabel, 1);
    statusBar()->addPermanentWidget(m_diffCountLabel);
}

void MainWindow::openPair() {
    const QString left = QFileDialog::getOpenFileName(this, "Open Left File");
    if (left.isEmpty()) return;
    const QString right = QFileDialog::getOpenFileName(this, "Open Right File");
    if (right.isEmpty()) return;
    loadPair(left, right);
}

void MainWindow::openLeft() {
    const QString p = QFileDialog::getOpenFileName(this, "Open Left File");
    if (p.isEmpty()) return;
    m_leftPath = p;
    tryLoadPair();
}

void MainWindow::openRight() {
    const QString p = QFileDialog::getOpenFileName(this, "Open Right File");
    if (p.isEmpty()) return;
    m_rightPath = p;
    tryLoadPair();
}

void MainWindow::openFolderPair() {
    const QString left = QFileDialog::getExistingDirectory(this, "Open Left Folder");
    if (left.isEmpty()) return;
    const QString right = QFileDialog::getExistingDirectory(this, "Open Right Folder");
    if (right.isEmpty()) return;
    loadFolderPair(left, right);
}

void MainWindow::loadPair(const QString& leftPath, const QString& rightPath) {
    m_leftPath = leftPath;
    m_rightPath = rightPath;

    if (DiffView* existing = findDiffTabForPair(leftPath, rightPath)) {
        m_tabs->setCurrentWidget(existing);
        return;
    }

    DiffView* view = createDiffTab(leftPath, rightPath);
    QString error;
    if (!view->setFiles(leftPath, rightPath, &error)) {
        QMessageBox::warning(this, "twain", error);
        const int idx = m_tabs->indexOf(view);
        if (idx >= 0) m_tabs->removeTab(idx);
        view->deleteLater();
        return;
    }
    m_tabs->setCurrentWidget(view);
    updateDiffTabTitle(view);
    rememberRecentPair("recent", leftPath, rightPath);
    updateForCurrentTab();
}

void MainWindow::loadFolderPair(const QString& leftPath, const QString& rightPath) {
    if (TreeCompareView* existing = findTreeTabForPair(leftPath, rightPath)) {
        m_tabs->setCurrentWidget(existing);
        return;
    }

    TreeCompareView* view = createTreeTab(leftPath, rightPath);
    QString error;
    if (!view->setFolders(leftPath, rightPath, &error)) {
        QMessageBox::warning(this, "twain", error);
        const int idx = m_tabs->indexOf(view);
        if (idx >= 0) m_tabs->removeTab(idx);
        view->deleteLater();
        return;
    }
    m_tabs->setCurrentWidget(view);
    updateTreeTabTitle(view);
    rememberRecentPair("recentFolders", leftPath, rightPath);
    updateForCurrentTab();
}

void MainWindow::refresh() {
    if (auto* v = currentDiffView()) {
        QString error;
        if (!v->setFiles(v->leftPath(), v->rightPath(), &error)) {
            QMessageBox::warning(this, "twain", error);
            return;
        }
        updateForCurrentTab();
    } else if (auto* t = currentTreeView()) {
        const QString lp = t->property("leftPath").toString();
        const QString rp = t->property("rightPath").toString();
        QString error;
        if (!t->setFolders(lp, rp, &error)) {
            QMessageBox::warning(this, "twain", error);
            return;
        }
        updateForCurrentTab();
    }
}

void MainWindow::nextDifference() {
    if (auto* v = currentDiffView()) v->nextDifference();
    else if (auto* t = currentTreeView()) t->nextDifferentFile(/*open=*/false);
}

void MainWindow::prevDifference() {
    if (auto* v = currentDiffView()) v->prevDifference();
    else if (auto* t = currentTreeView()) t->prevDifferentFile(/*open=*/false);
}

void MainWindow::nextDifferentFile() {
    if (auto* t = currentTreeView()) {
        t->nextDifferentFile(/*open=*/true);
        return;
    }
    if (auto* dv = currentDiffView()) {
        QObject* src = dv->property("sourceTree").value<QObject*>();
        auto* t = qobject_cast<TreeCompareView*>(src);
        if (!t) return;
        const auto paths = t->nextDifferentFile(/*open=*/false);
        if (paths.first.isEmpty() && paths.second.isEmpty()) return;
        QString error;
        if (!dv->setFiles(paths.first, paths.second, &error)) {
            QMessageBox::warning(this, "twain", error);
            return;
        }
        updateDiffTabTitle(dv);
        rememberRecentPair("recent", paths.first, paths.second);
        updateForCurrentTab();
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About twain",
                       "twain — a side-by-side diff tool.\nVersion 0.3.0");
}

void MainWindow::updatePathStatus() {
    QFontMetrics fm(m_leftPathLabel->font());
    m_leftPathLabel->setText(fm.elidedText(m_leftPath, Qt::ElideMiddle, 600));
    m_rightPathLabel->setText(fm.elidedText(m_rightPath, Qt::ElideMiddle, 600));
    m_leftPathLabel->setToolTip(m_leftPath);
    m_rightPathLabel->setToolTip(m_rightPath);
}

void MainWindow::updateFolderPathStatus() {
    // No-op stub kept for header compatibility — folder paths now come from the
    // active tree tab and are rendered via updateForCurrentTab().
}

void MainWindow::tryLoadPair() {
    if (m_leftPath.isEmpty() || m_rightPath.isEmpty()) return;
    loadPair(m_leftPath, m_rightPath);
}

DiffView* MainWindow::currentDiffView() const {
    return qobject_cast<DiffView*>(m_tabs->currentWidget());
}

TreeCompareView* MainWindow::currentTreeView() const {
    return qobject_cast<TreeCompareView*>(m_tabs->currentWidget());
}

DiffView* MainWindow::findDiffTabForPair(const QString& left, const QString& right) const {
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* v = qobject_cast<DiffView*>(m_tabs->widget(i));
        if (v && v->leftPath() == left && v->rightPath() == right) return v;
    }
    return nullptr;
}

TreeCompareView* MainWindow::findTreeTabForPair(const QString& left, const QString& right) const {
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* t = qobject_cast<TreeCompareView*>(m_tabs->widget(i));
        if (!t) continue;
        if (t->property("leftPath").toString() == left &&
            t->property("rightPath").toString() == right) return t;
    }
    return nullptr;
}

DiffView* MainWindow::createDiffTab(const QString& left, const QString& right) {
    auto* view = new DiffView(this);
    DiffView::Options opts;
    opts.ignoreCase = m_actIgnoreCase->isChecked();
    opts.ignoreWhitespace = m_actIgnoreWhitespace->isChecked();
    opts.ignoreBlankLines = m_actIgnoreBlankLines->isChecked();
    view->setOptions(opts);
    connect(view, &DiffView::currentDifferenceChanged, this,
            [this, view](int, int) {
                if (currentDiffView() == view) updateForCurrentTab();
            });
    const QString l = QFileInfo(left).fileName();
    const QString r = QFileInfo(right).fileName();
    const QString title = (l == r && !l.isEmpty()) ? l : QString("%1 ⟷ %2").arg(l, r);
    const int idx = m_tabs->addTab(view, title.isEmpty() ? "untitled" : title);
    m_tabs->setTabToolTip(idx, left + "\n" + right);
    return view;
}

TreeCompareView* MainWindow::createTreeTab(const QString& left, const QString& right) {
    auto* view = new TreeCompareView(this);
    view->setProperty("leftPath", left);
    view->setProperty("rightPath", right);
    connect(view, &TreeCompareView::fileActivated, this, &MainWindow::onFileActivatedFromTree);
    connect(view, &TreeCompareView::comparisonUpdated, this,
            [this, view]() {
                if (currentTreeView() == view) updateForCurrentTab();
            });
    QSettings s;
    const auto filter = static_cast<TreeCompareModel::FilterMode>(
        s.value("tree/filter", int(TreeCompareModel::FilterMode::DifferencesOnly)).toInt());
    view->setFilter(filter);
    if (s.contains("tree/splitter")) view->restoreSplitterState(s.value("tree/splitter").toByteArray());
    if (s.contains("tree/header")) view->restoreHeaderState(s.value("tree/header").toByteArray());

    const QString l = QFileInfo(left).fileName();
    const QString r = QFileInfo(right).fileName();
    const QString title = (l == r && !l.isEmpty()) ? l + "/" : QString("%1/ ⟷ %2/").arg(l, r);
    const int idx = m_tabs->addTab(view, title);
    m_tabs->setTabToolTip(idx, left + "\n" + right);
    return view;
}

void MainWindow::updateDiffTabTitle(DiffView* view) {
    const int idx = m_tabs->indexOf(view);
    if (idx < 0) return;
    const QString l = QFileInfo(view->leftPath()).fileName();
    const QString r = QFileInfo(view->rightPath()).fileName();
    const QString title = (l == r && !l.isEmpty()) ? l : QString("%1 ⟷ %2").arg(l, r);
    m_tabs->setTabText(idx, title.isEmpty() ? "untitled" : title);
    m_tabs->setTabToolTip(idx, view->leftPath() + "\n" + view->rightPath());
}

void MainWindow::updateTreeTabTitle(TreeCompareView* view) {
    const int idx = m_tabs->indexOf(view);
    if (idx < 0) return;
    const QString lp = view->property("leftPath").toString();
    const QString rp = view->property("rightPath").toString();
    const QString l = QFileInfo(lp).fileName();
    const QString r = QFileInfo(rp).fileName();
    const QString title = (l == r && !l.isEmpty()) ? l + "/" : QString("%1/ ⟷ %2/").arg(l, r);
    m_tabs->setTabText(idx, title);
    m_tabs->setTabToolTip(idx, lp + "\n" + rp);
}

void MainWindow::updateForCurrentTab() {
    DiffView* dv = currentDiffView();
    TreeCompareView* tv = currentTreeView();

    const bool isDiff = (dv != nullptr);
    const bool isTree = (tv != nullptr);

    m_actNextDiff->setVisible(isDiff || isTree);
    m_actPrevDiff->setVisible(isDiff || isTree);
    m_filterComboAction->setVisible(isTree);
    m_actRefresh->setEnabled(isDiff || isTree);

    if (isDiff) {
        m_leftPath = dv->leftPath();
        m_rightPath = dv->rightPath();
        updatePathStatus();
        const int n = dv->differenceCount();
        const int cur = dv->currentDifference();
        if (n == 0) {
            m_diffCountLabel->setText("No differences");
        } else if (cur < 0) {
            m_diffCountLabel->setText(QString("%1 differences").arg(n));
        } else {
            m_diffCountLabel->setText(QString("Diff %1 / %2").arg(cur + 1).arg(n));
        }
        m_actNextDiff->setEnabled(n > 0);
        m_actPrevDiff->setEnabled(n > 0);
        m_actNextDiffFile->setEnabled(true);
        setWindowTitle(QString("twain — %1 ⟷ %2")
                           .arg(QFileInfo(dv->leftPath()).fileName(),
                                QFileInfo(dv->rightPath()).fileName()));
    } else if (isTree) {
        const QString lp = tv->property("leftPath").toString();
        const QString rp = tv->property("rightPath").toString();
        QFontMetrics fm(m_leftPathLabel->font());
        m_leftPathLabel->setText(fm.elidedText(lp, Qt::ElideMiddle, 600));
        m_rightPathLabel->setText(fm.elidedText(rp, Qt::ElideMiddle, 600));
        m_leftPathLabel->setToolTip(lp);
        m_rightPathLabel->setToolTip(rp);
        const int s = tv->sameCount();
        const int d = tv->differentCount();
        const int l = tv->leftOnlyCount();
        const int r = tv->rightOnlyCount();
        m_diffCountLabel->setText(QString("%1 same · %2 different · %3← · %4→").arg(s).arg(d).arg(l).arg(r));
        m_actNextDiff->setEnabled(true);
        m_actPrevDiff->setEnabled(true);
        m_actNextDiffFile->setEnabled(true);
        for (int i = 0; i < m_filterCombo->count(); ++i) {
            if (m_filterCombo->itemData(i).toInt() == int(tv->filter())) {
                const QSignalBlocker blocker(m_filterCombo);
                m_filterCombo->setCurrentIndex(i);
                break;
            }
        }
        setWindowTitle(QString("twain — %1/ ⟷ %2/")
                           .arg(QFileInfo(lp).fileName(), QFileInfo(rp).fileName()));
    } else {
        m_leftPathLabel->clear();
        m_rightPathLabel->clear();
        m_diffCountLabel->clear();
        m_actNextDiff->setEnabled(false);
        m_actPrevDiff->setEnabled(false);
        m_actNextDiffFile->setEnabled(false);
        setWindowTitle("twain");
    }
}

void MainWindow::onTabChanged(int /*index*/) {
    updateForCurrentTab();
}

void MainWindow::onTabCloseRequested(int index) {
    QWidget* w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (w) w->deleteLater();
    updateForCurrentTab();
}

void MainWindow::applyDiffOptionsToAllTabs() {
    DiffView::Options opts;
    opts.ignoreCase = m_actIgnoreCase->isChecked();
    opts.ignoreWhitespace = m_actIgnoreWhitespace->isChecked();
    opts.ignoreBlankLines = m_actIgnoreBlankLines->isChecked();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* dv = qobject_cast<DiffView*>(m_tabs->widget(i))) {
            dv->setOptions(opts);
        }
    }
    updateForCurrentTab();
}

void MainWindow::onFileActivatedFromTree(const QString& leftPath, const QString& rightPath) {
    auto* source = qobject_cast<TreeCompareView*>(sender());
    loadPair(leftPath, rightPath);
    if (auto* dv = currentDiffView()) {
        dv->setProperty("sourceTree", QVariant::fromValue<QObject*>(source));
    }
}

void MainWindow::onFolderComparisonUpdated() {
    updateForCurrentTab();
}

void MainWindow::readSettings() {
    QSettings s;
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("windowState")) restoreState(s.value("windowState").toByteArray());

    const int filterInt = s.value("tree/filter", int(TreeCompareModel::FilterMode::DifferencesOnly)).toInt();
    for (int i = 0; i < m_filterCombo->count(); ++i) {
        if (m_filterCombo->itemData(i).toInt() == filterInt) {
            const QSignalBlocker blocker(m_filterCombo);
            m_filterCombo->setCurrentIndex(i);
            break;
        }
    }

    {
        const QSignalBlocker b1(m_actIgnoreCase);
        const QSignalBlocker b2(m_actIgnoreWhitespace);
        const QSignalBlocker b3(m_actIgnoreBlankLines);
        m_actIgnoreCase->setChecked(s.value("diff/ignoreCase", false).toBool());
        m_actIgnoreWhitespace->setChecked(s.value("diff/ignoreWhitespace", false).toBool());
        m_actIgnoreBlankLines->setChecked(s.value("diff/ignoreBlankLines", false).toBool());
    }
}

void MainWindow::writeSettings() {
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    if (auto* tv = currentTreeView()) {
        s.setValue("tree/splitter", tv->saveSplitterState());
        s.setValue("tree/header", tv->saveHeaderState());
        s.setValue("tree/filter", int(tv->filter()));
    }
    s.setValue("diff/ignoreCase", m_actIgnoreCase->isChecked());
    s.setValue("diff/ignoreWhitespace", m_actIgnoreWhitespace->isChecked());
    s.setValue("diff/ignoreBlankLines", m_actIgnoreBlankLines->isChecked());
}

void MainWindow::rememberRecentPair(const QString& prefix, const QString& left, const QString& right) {
    QSettings s;
    QStringList lefts, rights;
    for (int i = 0; i < kMaxRecent; ++i) {
        const QString l = s.value(recentKey(prefix, i, true)).toString();
        const QString r = s.value(recentKey(prefix, i, false)).toString();
        if (l.isEmpty() || r.isEmpty()) break;
        if (l == left && r == right) continue;
        lefts.append(l);
        rights.append(r);
    }
    lefts.prepend(left);
    rights.prepend(right);
    while (lefts.size() > kMaxRecent) {
        lefts.removeLast();
        rights.removeLast();
    }
    for (int i = 0; i < kMaxRecent; ++i) {
        if (i < lefts.size()) {
            s.setValue(recentKey(prefix, i, true), lefts[i]);
            s.setValue(recentKey(prefix, i, false), rights[i]);
        } else {
            s.remove(recentKey(prefix, i, true));
            s.remove(recentKey(prefix, i, false));
        }
    }
}

void MainWindow::rebuildRecentMenu() {
    m_recentMenu->clear();
    QSettings s;
    bool any = false;
    for (int i = 0; i < kMaxRecent; ++i) {
        const QString l = s.value(recentKey("recent", i, true)).toString();
        const QString r = s.value(recentKey("recent", i, false)).toString();
        if (l.isEmpty() || r.isEmpty()) break;
        any = true;
        const QString label = QString("%1 ⟷ %2")
                                  .arg(QFileInfo(l).fileName(), QFileInfo(r).fileName());
        QAction* a = m_recentMenu->addAction(label);
        a->setToolTip(l + "\n" + r);
        connect(a, &QAction::triggered, this, [this, l, r]() { loadPair(l, r); });
    }
    if (!any) {
        QAction* a = m_recentMenu->addAction("(none)");
        a->setEnabled(false);
    }
}

void MainWindow::rebuildRecentFoldersMenu() {
    m_recentFoldersMenu->clear();
    QSettings s;
    bool any = false;
    for (int i = 0; i < kMaxRecent; ++i) {
        const QString l = s.value(recentKey("recentFolders", i, true)).toString();
        const QString r = s.value(recentKey("recentFolders", i, false)).toString();
        if (l.isEmpty() || r.isEmpty()) break;
        any = true;
        const QString label = QString("%1/ ⟷ %2/")
                                  .arg(QFileInfo(l).fileName(), QFileInfo(r).fileName());
        QAction* a = m_recentFoldersMenu->addAction(label);
        a->setToolTip(l + "\n" + r);
        connect(a, &QAction::triggered, this, [this, l, r]() { loadFolderPair(l, r); });
    }
    if (!any) {
        QAction* a = m_recentFoldersMenu->addAction("(none)");
        a->setEnabled(false);
    }
}
