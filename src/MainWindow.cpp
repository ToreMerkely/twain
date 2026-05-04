#include "MainWindow.h"

#include "DiffView.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
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
#include <QToolBar>

namespace {
constexpr int kMaxRecent = 10;
QString recentKey(int i, bool left) {
    return QString("recent/%1/%2").arg(i).arg(left ? "left" : "right");
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
    setCentralWidget(m_diffView);

    connect(m_diffView, &DiffView::currentDifferenceChanged, this,
            [this](int idx, int total) {
                if (total == 0) {
                    m_diffCountLabel->setText("No differences");
                } else {
                    m_diffCountLabel->setText(QString("Diff %1 / %2").arg(idx + 1).arg(total));
                }
            });

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

    m_actAbout = new QAction("&About", this);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_actOpenPair);
    fileMenu->addAction(m_actOpenLeft);
    fileMenu->addAction(m_actOpenRight);
    m_recentMenu = fileMenu->addMenu("Recent &Pairs");
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actRefresh);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_actNextDiff);
    viewMenu->addAction(m_actPrevDiff);

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction(m_actAbout);
}

void MainWindow::createToolBar() {
    auto* tb = addToolBar("Main");
    tb->setObjectName("MainToolBar");
    tb->addAction(m_actOpenPair);
    tb->addAction(m_actRefresh);
    tb->addSeparator();
    tb->addAction(m_actPrevDiff);
    tb->addAction(m_actNextDiff);
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
    updatePathStatus();
    tryLoadPair();
}

void MainWindow::openRight() {
    const QString p = QFileDialog::getOpenFileName(this, "Open Right File");
    if (p.isEmpty()) return;
    m_rightPath = p;
    updatePathStatus();
    tryLoadPair();
}

void MainWindow::loadPair(const QString& leftPath, const QString& rightPath) {
    m_leftPath = leftPath;
    m_rightPath = rightPath;
    updatePathStatus();
    tryLoadPair();
}

void MainWindow::refresh() {
    tryLoadPair();
}

void MainWindow::nextDifference() {
    if (m_diffView) m_diffView->nextDifference();
}

void MainWindow::prevDifference() {
    if (m_diffView) m_diffView->prevDifference();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About twain",
                       "twain — a side-by-side diff tool.\nVersion 0.1.0");
}

void MainWindow::updatePathStatus() {
    QFontMetrics fm(m_leftPathLabel->font());
    m_leftPathLabel->setText(fm.elidedText(m_leftPath, Qt::ElideMiddle, 600));
    m_rightPathLabel->setText(fm.elidedText(m_rightPath, Qt::ElideMiddle, 600));
    m_leftPathLabel->setToolTip(m_leftPath);
    m_rightPathLabel->setToolTip(m_rightPath);
}

void MainWindow::tryLoadPair() {
    if (m_leftPath.isEmpty() || m_rightPath.isEmpty()) return;
    QString error;
    if (!m_diffView->setFiles(m_leftPath, m_rightPath, &error)) {
        QMessageBox::warning(this, "twain", error);
        return;
    }
    rememberRecent(m_leftPath, m_rightPath);
    setWindowTitle(QString("twain — %1 ⟷ %2")
                       .arg(QFileInfo(m_leftPath).fileName(),
                            QFileInfo(m_rightPath).fileName()));
    m_actRefresh->setEnabled(true);
    const int n = m_diffView->differenceCount();
    if (n == 0) m_diffCountLabel->setText("No differences");
    m_actNextDiff->setEnabled(n > 0);
    m_actPrevDiff->setEnabled(n > 0);
}

void MainWindow::readSettings() {
    QSettings s;
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("windowState")) restoreState(s.value("windowState").toByteArray());
    if (s.contains("splitter")) m_diffView->restoreSplitterState(s.value("splitter").toByteArray());
}

void MainWindow::writeSettings() {
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("splitter", m_diffView->saveSplitterState());
}

void MainWindow::rememberRecent(const QString& left, const QString& right) {
    QSettings s;
    QStringList lefts, rights;
    for (int i = 0; i < kMaxRecent; ++i) {
        const QString l = s.value(recentKey(i, true)).toString();
        const QString r = s.value(recentKey(i, false)).toString();
        if (l.isEmpty() || r.isEmpty()) break;
        if (l == left && r == right) continue;  // dedup
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
            s.setValue(recentKey(i, true), lefts[i]);
            s.setValue(recentKey(i, false), rights[i]);
        } else {
            s.remove(recentKey(i, true));
            s.remove(recentKey(i, false));
        }
    }
}

void MainWindow::rebuildRecentMenu() {
    m_recentMenu->clear();
    QSettings s;
    bool any = false;
    for (int i = 0; i < kMaxRecent; ++i) {
        const QString l = s.value(recentKey(i, true)).toString();
        const QString r = s.value(recentKey(i, false)).toString();
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
