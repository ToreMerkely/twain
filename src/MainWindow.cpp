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
#include <QStackedWidget>
#include <QStatusBar>

#include "TreeCompareModel.h"
#include <QStyle>
#include <QToolBar>

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

    m_diffView = new DiffView(this);
    m_treeView = new TreeCompareView(this);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_diffView);   // index 0 = file
    m_stack->addWidget(m_treeView);   // index 1 = folder
    setCentralWidget(m_stack);
    m_treeView->setFilter(TreeCompareModel::FilterMode::DifferencesOnly);

    connect(m_diffView, &DiffView::currentDifferenceChanged, this,
            [this](int idx, int total) {
                if (m_mode != Mode::File) return;
                if (total == 0) {
                    m_diffCountLabel->setText("No differences");
                } else {
                    m_diffCountLabel->setText(QString("Diff %1 / %2").arg(idx + 1).arg(total));
                }
            });

    connect(m_treeView, &TreeCompareView::fileActivated, this, &MainWindow::onFileActivatedFromTree);
    connect(m_treeView, &TreeCompareView::comparisonUpdated, this, &MainWindow::onFolderComparisonUpdated);

    setMode(Mode::File);
    readSettings();
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
    m_actNextDiff->setShortcut(Qt::Key_F7);
    m_actNextDiff->setEnabled(false);
    connect(m_actNextDiff, &QAction::triggered, this, &MainWindow::nextDifference);

    m_actPrevDiff = new QAction(icon(QStyle::SP_ArrowUp), "&Previous Difference", this);
    m_actPrevDiff->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7));
    m_actPrevDiff->setEnabled(false);
    connect(m_actPrevDiff, &QAction::triggered, this, &MainWindow::prevDifference);

    m_actBack = new QAction(icon(QStyle::SP_ArrowBack), "&Back to Folder", this);
    m_actBack->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    m_actBack->setEnabled(false);
    connect(m_actBack, &QAction::triggered, this, &MainWindow::back);

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
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_actNextDiff);
    viewMenu->addAction(m_actPrevDiff);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actBack);

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
    tb->addAction(m_actBack);
    tb->addAction(m_actPrevDiff);
    tb->addAction(m_actNextDiff);
    tb->addSeparator();

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem("All", int(TreeCompareModel::FilterMode::All));
    m_filterCombo->addItem("Differences only", int(TreeCompareModel::FilterMode::DifferencesOnly));
    m_filterCombo->addItem("Left only", int(TreeCompareModel::FilterMode::LeftOnly));
    m_filterCombo->addItem("Right only", int(TreeCompareModel::FilterMode::RightOnly));
    m_filterCombo->setCurrentIndex(1);  // DifferencesOnly default
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const auto mode = static_cast<TreeCompareModel::FilterMode>(
                    m_filterCombo->currentData().toInt());
                m_treeView->setFilter(mode);
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

void MainWindow::setMode(Mode m) {
    m_mode = m;
    m_stack->setCurrentIndex(m == Mode::File ? 0 : 1);

    const bool isFile = (m == Mode::File);
    m_actNextDiff->setVisible(isFile);
    m_actPrevDiff->setVisible(isFile);
    m_actBack->setVisible(isFile && !m_lastFolderPairLeft.isEmpty());
    m_actBack->setEnabled(isFile && !m_lastFolderPairLeft.isEmpty());
    if (m_filterComboAction) m_filterComboAction->setVisible(!isFile);
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
    if (m_mode != Mode::File) setMode(Mode::File);
    updatePathStatus();
    tryLoadPair();
}

void MainWindow::openRight() {
    const QString p = QFileDialog::getOpenFileName(this, "Open Right File");
    if (p.isEmpty()) return;
    m_rightPath = p;
    if (m_mode != Mode::File) setMode(Mode::File);
    updatePathStatus();
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
    setMode(Mode::File);
    updatePathStatus();
    tryLoadPair();
}

void MainWindow::loadFolderPair(const QString& leftPath, const QString& rightPath) {
    m_leftFolder = leftPath;
    m_rightFolder = rightPath;
    m_lastFolderPairLeft = leftPath;
    m_lastFolderPairRight = rightPath;
    setMode(Mode::Folder);
    QString error;
    if (!m_treeView->setFolders(leftPath, rightPath, &error)) {
        QMessageBox::warning(this, "twain", error);
        return;
    }
    rememberRecentPair("recentFolders", leftPath, rightPath);
    setWindowTitle(QString("twain — %1/ ⟷ %2/")
                       .arg(QFileInfo(leftPath).fileName(),
                            QFileInfo(rightPath).fileName()));
    m_actRefresh->setEnabled(true);
    updateFolderPathStatus();
}

void MainWindow::refresh() {
    if (m_mode == Mode::Folder) {
        if (!m_leftFolder.isEmpty() && !m_rightFolder.isEmpty()) {
            loadFolderPair(m_leftFolder, m_rightFolder);
        }
    } else {
        tryLoadPair();
    }
}

void MainWindow::nextDifference() {
    if (m_diffView) m_diffView->nextDifference();
}

void MainWindow::prevDifference() {
    if (m_diffView) m_diffView->prevDifference();
}

void MainWindow::back() {
    if (!m_lastFolderPairLeft.isEmpty() && !m_lastFolderPairRight.isEmpty()) {
        setMode(Mode::Folder);
        updateFolderPathStatus();
        onFolderComparisonUpdated();
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About twain",
                       "twain — a side-by-side diff tool.\nVersion 0.2.0");
}

void MainWindow::updatePathStatus() {
    QFontMetrics fm(m_leftPathLabel->font());
    m_leftPathLabel->setText(fm.elidedText(m_leftPath, Qt::ElideMiddle, 600));
    m_rightPathLabel->setText(fm.elidedText(m_rightPath, Qt::ElideMiddle, 600));
    m_leftPathLabel->setToolTip(m_leftPath);
    m_rightPathLabel->setToolTip(m_rightPath);
}

void MainWindow::updateFolderPathStatus() {
    QFontMetrics fm(m_leftPathLabel->font());
    m_leftPathLabel->setText(fm.elidedText(m_leftFolder, Qt::ElideMiddle, 600));
    m_rightPathLabel->setText(fm.elidedText(m_rightFolder, Qt::ElideMiddle, 600));
    m_leftPathLabel->setToolTip(m_leftFolder);
    m_rightPathLabel->setToolTip(m_rightFolder);
}

void MainWindow::tryLoadPair() {
    if (m_leftPath.isEmpty() && m_rightPath.isEmpty()) return;
    QString error;
    if (!m_diffView->setFiles(m_leftPath, m_rightPath, &error)) {
        QMessageBox::warning(this, "twain", error);
        return;
    }
    rememberRecentPair("recent", m_leftPath, m_rightPath);
    setWindowTitle(QString("twain — %1 ⟷ %2")
                       .arg(QFileInfo(m_leftPath).fileName(),
                            QFileInfo(m_rightPath).fileName()));
    m_actRefresh->setEnabled(true);
    const int n = m_diffView->differenceCount();
    if (n == 0) m_diffCountLabel->setText("No differences");
    m_actNextDiff->setEnabled(n > 0);
    m_actPrevDiff->setEnabled(n > 0);
}

void MainWindow::onFileActivatedFromTree(const QString& leftPath, const QString& rightPath) {
    m_lastFolderPairLeft = m_leftFolder;
    m_lastFolderPairRight = m_rightFolder;
    loadPair(leftPath, rightPath);
}

void MainWindow::onFolderComparisonUpdated() {
    if (m_mode != Mode::Folder) return;
    const int s = m_treeView->sameCount();
    const int d = m_treeView->differentCount();
    const int l = m_treeView->leftOnlyCount();
    const int r = m_treeView->rightOnlyCount();
    m_diffCountLabel->setText(QString("%1 same · %2 different · %3← · %4→").arg(s).arg(d).arg(l).arg(r));
}

void MainWindow::readSettings() {
    QSettings s;
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("windowState")) restoreState(s.value("windowState").toByteArray());
    if (s.contains("splitter")) m_diffView->restoreSplitterState(s.value("splitter").toByteArray());
    if (s.contains("tree/splitter")) m_treeView->restoreSplitterState(s.value("tree/splitter").toByteArray());
    if (s.contains("tree/header")) m_treeView->restoreHeaderState(s.value("tree/header").toByteArray());

    const int filterInt = s.value("tree/filter", int(TreeCompareModel::FilterMode::DifferencesOnly)).toInt();
    const auto filter = static_cast<TreeCompareModel::FilterMode>(filterInt);
    m_treeView->setFilter(filter);
    for (int i = 0; i < m_filterCombo->count(); ++i) {
        if (m_filterCombo->itemData(i).toInt() == filterInt) {
            m_filterCombo->setCurrentIndex(i);
            break;
        }
    }

    if (s.value("lastMode", 0).toInt() == 1) setMode(Mode::Folder);
}

void MainWindow::writeSettings() {
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("splitter", m_diffView->saveSplitterState());
    s.setValue("tree/splitter", m_treeView->saveSplitterState());
    s.setValue("tree/header", m_treeView->saveHeaderState());
    s.setValue("tree/filter", int(m_treeView->filter()));
    s.setValue("lastMode", m_stack->currentIndex());
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
