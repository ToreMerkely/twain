#include "MainWindow.h"

#include "DiffView.h"
#include "TreeCompareView.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
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
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QEventLoop>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include "DebugLog.h"
#include "TreeCompareModel.h"

namespace {

// A status-bar busy indicator that draws a blue box sweeping back and forth.
// Starts/stops its internal timer when shown/hidden, so callers only need to
// toggle visibility.
class BusyIndicator : public QWidget {
public:
    explicit BusyIndicator(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(200, 14);
        m_timer.setInterval(30);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this]() {
            const int maxPos = width() - kBoxWidth;
            m_pos += m_dir * kStep;
            if (m_pos >= maxPos) { m_pos = maxPos; m_dir = -1; }
            else if (m_pos <= 0) { m_pos = 0; m_dir = 1; }
            update();
        });
    }
protected:
    void showEvent(QShowEvent* e) override {
        m_pos = 0; m_dir = 1;
        m_timer.start();
        QWidget::showEvent(e);
    }
    void hideEvent(QHideEvent* e) override {
        m_timer.stop();
        QWidget::hideEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(240, 240, 240));
        p.setPen(QColor(204, 204, 204));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        p.fillRect(QRect(m_pos, 1, kBoxWidth, height() - 2), QColor(52, 120, 255));
    }
private:
    static constexpr int kBoxWidth = 40;
    static constexpr int kStep = 5;
    QTimer m_timer;
    int m_pos = 0;
    int m_dir = 1;
};

constexpr int kMaxRecent = 10;
QString recentKey(const QString& prefix, int i, bool left) {
    return QString("%1/%2/%3").arg(prefix).arg(i).arg(left ? "left" : "right");
}
QString dirDisplayName(const QString& p) {
    QString s = p;
    while (s.size() > 1 && s.endsWith('/')) s.chop(1);
    const QString name = QFileInfo(s).fileName();
    return name.isEmpty() ? s : name;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("twain");
    resize(1400, 900);
    // Auto-fill the window background with the palette colour so first-paint
    // can never show through to whatever was in the underlying X buffer.
    // (WA_OpaquePaintEvent together with this attribute confused Qt into
    // suppressing paint events entirely on at least one compositor.)
    setAutoFillBackground(true);

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setAutoFillBackground(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    readSettings();
    updateForCurrentTab();
    qApp->installEventFilter(this);
}

void MainWindow::showEvent(QShowEvent* event) {
    static bool firstShow = true;
    if (firstShow) { TWAIN_LOG("MainWindow::showEvent first"); firstShow = false; }
    QMainWindow::showEvent(event);
}

void MainWindow::paintEvent(QPaintEvent* event) {
    static bool firstPaint = true;
    if (firstPaint) { TWAIN_LOG("MainWindow::paintEvent first"); firstPaint = false; }
    QMainWindow::paintEvent(event);
}

bool MainWindow::event(QEvent* event) {
    static bool firstExpose = true;
    if (firstExpose && event->type() == QEvent::Expose) {
        TWAIN_LOG("MainWindow::event Expose first (window mapped)");
        firstExpose = false;
    }
    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->matches(QKeySequence::Undo)) {
            // Reserve Ctrl+Z for our handler; otherwise QPlainTextEdit's
            // internal binding eats it first and our QAction never fires.
            QWidget* focused = QApplication::focusWidget();
            if (auto* pte = qobject_cast<QPlainTextEdit*>(focused)) {
                if (pte->document()->isUndoAvailable()) {
                    return false;  // let pte handle it
                }
            }
            undo();
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* dv = qobject_cast<DiffView*>(m_tabs->widget(i)); dv && dv->isDirty()) {
            const auto reply = QMessageBox::question(
                this, "twain",
                "There are unsaved changes. Save all before quitting?",
                QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel);
            if (reply == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (reply == QMessageBox::SaveAll) {
                for (int j = 0; j < m_tabs->count(); ++j) {
                    if (auto* dv2 = qobject_cast<DiffView*>(m_tabs->widget(j)); dv2 && dv2->isDirty()) {
                        QString error;
                        if (!dv2->save(&error)) {
                            QMessageBox::warning(this, "twain", error);
                            event->ignore();
                            return;
                        }
                    }
                }
            }
            break;
        }
    }
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

    m_actSave = new QAction(icon(QStyle::SP_DialogSaveButton), "&Save", this);
    m_actSave->setShortcut(QKeySequence::Save);
    m_actSave->setEnabled(false);
    connect(m_actSave, &QAction::triggered, this, &MainWindow::save);

    m_actUndo = new QAction("&Undo", this);
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actUndo->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_actUndo, &QAction::triggered, this, &MainWindow::undo);
    addAction(m_actUndo);

    auto* findAct = new QAction("&Find", this);
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, [this]() {
        if (auto* dv = currentDiffView()) dv->showSearchBar();
    });
    addAction(findAct);

    m_actCloseTab = new QAction("&Close Tab", this);
    m_actCloseTab->setShortcut(QKeySequence::Close);  // Ctrl+W
    connect(m_actCloseTab, &QAction::triggered, this, [this]() {
        const int idx = m_tabs->currentIndex();
        if (idx >= 0) onTabCloseRequested(idx);
    });
    addAction(m_actCloseTab);

    auto* nextTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), this);
    nextTab->setContext(Qt::ApplicationShortcut);
    connect(nextTab, &QShortcut::activated, this, [this]() {
        const int n = m_tabs->count();
        if (n <= 0) return;
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % n);
    });
    auto* prevTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), this);
    prevTab->setContext(Qt::ApplicationShortcut);
    connect(prevTab, &QShortcut::activated, this, [this]() {
        const int n = m_tabs->count();
        if (n <= 0) return;
        m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + n) % n);
    });

    auto makeToggle = [this](const QString& label) {
        auto* a = new QAction(label, this);
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this](bool) { applyDiffOptionsToAllTabs(); });
        return a;
    };
    m_actIgnoreCase = makeToggle("Ignore &Case");
    m_actIgnoreWhitespace = makeToggle("Ignore &Whitespace");
    m_actIgnoreBlankLines = makeToggle("Ignore &Blank Lines");

    m_actZoomIn = new QAction("Zoom &In", this);
    m_actZoomIn->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                               QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(m_actZoomIn, &QAction::triggered, this,
            []() { DiffView::adjustDiffFontPt(+1); });
    addAction(m_actZoomIn);

    m_actZoomOut = new QAction("Zoom &Out", this);
    m_actZoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(m_actZoomOut, &QAction::triggered, this,
            []() { DiffView::adjustDiffFontPt(-1); });
    addAction(m_actZoomOut);

    m_actZoomReset = new QAction("&Reset Zoom", this);
    m_actZoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_actZoomReset, &QAction::triggered, this,
            []() { DiffView::setDiffFontPt(10); });
    addAction(m_actZoomReset);

    m_actAbout = new QAction("&About", this);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);

    m_actGitDifftool = new QAction("Configure as git &difftool...", this);
    connect(m_actGitDifftool, &QAction::triggered, this, &MainWindow::showGitDiffToolHelp);
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
    fileMenu->addAction(m_actSave);
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
    viewMenu->addSeparator();
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomReset);

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction(m_actGitDifftool);
    helpMenu->addAction(m_actAbout);
}

void MainWindow::createToolBar() {
    auto* tb = addToolBar("Main");
    tb->setObjectName("MainToolBar");
    tb->addAction(m_actOpenPair);
    tb->addAction(m_actOpenFolderPair);
    tb->addAction(m_actSave);
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
    // Parented to the status bar but kept out of its layout so it can float
    // centered as an overlay. Position is set in resizeEvent / setBusy.
    m_busyBar = new BusyIndicator(statusBar());
    m_busyBar->hide();
    statusBar()->addWidget(m_leftPathLabel, 1);
    statusBar()->addWidget(m_rightPathLabel, 1);
    statusBar()->addPermanentWidget(m_diffCountLabel);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_busyBar && m_busyBar->parentWidget() == statusBar()) {
        const int x = (statusBar()->width() - m_busyBar->width()) / 2;
        const int y = (statusBar()->height() - m_busyBar->height()) / 2;
        m_busyBar->move(x, y);
    }
}

void MainWindow::setBusy(const QString& message) {
    statusBar()->showMessage(message);
    if (m_busyBar) {
        // Recenter in case the status bar dimensions changed since last show.
        const int x = (statusBar()->width() - m_busyBar->width()) / 2;
        const int y = (statusBar()->height() - m_busyBar->height()) / 2;
        m_busyBar->move(x, y);
        m_busyBar->show();
        m_busyBar->raise();
    }
    // Drain pending paint events so the user sees the message and spinner
    // before any blocking work starts.
    QApplication::processEvents();
}

void MainWindow::clearBusy() {
    statusBar()->clearMessage();
    if (m_busyBar) m_busyBar->hide();
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

QString MainWindow::extractIfArchive(const QString& path) {
    static const QStringList tarExts = {
        ".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz",
    };
    const QString lower = path.toLower();
    bool isTar = false;
    for (const auto& ext : tarExts) {
        if (lower.endsWith(ext)) { isTar = true; break; }
    }
    const bool isZip = lower.endsWith(".zip");
    if (!isTar && !isZip) return path;

    auto tmp = std::make_unique<QTemporaryDir>();
    if (!tmp->isValid()) {
        QMessageBox::warning(this, "twain",
            QString("Failed to create temp directory for %1:\n%2")
                .arg(path, tmp->errorString()));
        return {};
    }
    const QString tmpPath = tmp->path();

    QProcess proc;
    QEventLoop loop;
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    if (isZip) {
        proc.start("unzip", {"-q", "-d", tmpPath, path});
    } else {
        proc.start("tar", {"-xf", path, "-C", tmpPath});
    }
    // Local event loop keeps the GUI responsive (status-bar spinner animates,
    // window repaints) while tar/unzip runs.
    loop.exec();
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok) {
        const QString stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        QMessageBox::warning(this, "twain",
            QString("Failed to extract %1:\n%2")
                .arg(path, stderrText.isEmpty() ? "(no stderr)" : stderrText));
        return {};
    }

    m_extractedArchives.push_back(std::move(tmp));
    return tmpPath;
}

void MainWindow::loadFromCli(const QString& leftArg, const QString& rightArg) {
    setBusy("Extracting…");
    const QString left = extractIfArchive(leftArg);
    const QString right = extractIfArchive(rightArg);
    if (left.isEmpty() || right.isEmpty()) {
        clearBusy();
        return;
    }
    setBusy("Loading…");
    const QFileInfo l(left);
    const QFileInfo r(right);
    if (l.isDir() && r.isDir()) {
        loadFolderPair(left, right);
    } else {
        loadPair(left, right);
    }
    clearBusy();
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

void MainWindow::save() {
    auto* dv = currentDiffView();
    if (!dv || !dv->isDirty()) return;
    QString error;
    if (!dv->save(&error)) {
        QMessageBox::warning(this, "twain", error);
    }
}

void MainWindow::undo() {
    QWidget* focused = QApplication::focusWidget();
    if (auto* pte = qobject_cast<QPlainTextEdit*>(focused)) {
        if (pte->document()->isUndoAvailable()) {
            pte->undo();
            return;
        }
    }
    if (auto* dv = currentDiffView()) {
        if (dv->canUndoArrow()) dv->undoArrow();
    }
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
        if (paths.first.isEmpty() && paths.second.isEmpty()) {
            const int idx = m_tabs->indexOf(dv);
            if (idx >= 0) onTabCloseRequested(idx);
            return;
        }
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

void MainWindow::showGitDiffToolHelp() {
    const QString bin = QCoreApplication::applicationFilePath();
    const QString cmd1 = QString("git config --global diff.tool twain");
    const QString cmd2 =
        QString("git config --global difftool.twain.cmd '%1 \"$LOCAL\" \"$REMOTE\"'").arg(bin);
    const QString cmd3 = QString("git config --global difftool.prompt false");
    const QString body = QString(
        "<p>Run these once to register twain as a git difftool:</p>"
        "<pre>%1\n%2\n%3</pre>"
        "<p>Then use it from any repo:</p>"
        "<pre>git difftool -d                # tree view: all changed files at once\n"
        "git difftool -d HEAD~1         # tree view vs a revision\n"
        "git difftool -d HEAD~1 HEAD    # what a commit changed (like 'git show')\n"
        "git difftool                   # one file at a time (default)</pre>"
        "<p>The <code>-d</code> / <code>--dir-diff</code> flag is what gives you the "
        "side-by-side folder tree. Without it, git invokes the "
        "difftool once per changed file.</p>"
        "<p>Drop <code>--global</code> to set per-repo only. A handy alias:</p>"
        "<pre>git config --global alias.dd 'difftool --dir-diff'</pre>")
        .arg(cmd1.toHtmlEscaped(), cmd2.toHtmlEscaped(), cmd3.toHtmlEscaped());

    QMessageBox box(this);
    box.setWindowTitle("Configure as git difftool");
    box.setTextFormat(Qt::RichText);
    box.setText(body);
    QPushButton* copyBtn = box.addButton("Copy commands", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == copyBtn) {
        QApplication::clipboard()->setText(cmd1 + "\n" + cmd2 + "\n" + cmd3);
    }
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
    connect(view, &DiffView::dirtyChanged, this, [this, view](bool) {
        updateDiffTabTitle(view);
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

    const QString l = dirDisplayName(left);
    const QString r = dirDisplayName(right);
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
    QString title = (l == r && !l.isEmpty()) ? l : QString("%1 ⟷ %2").arg(l, r);
    if (title.isEmpty()) title = "untitled";
    if (view->isDirty()) title += " *";
    m_tabs->setTabText(idx, title);
    m_tabs->setTabToolTip(idx, view->leftPath() + "\n" + view->rightPath());
}

void MainWindow::updateTreeTabTitle(TreeCompareView* view) {
    const int idx = m_tabs->indexOf(view);
    if (idx < 0) return;
    const QString lp = view->property("leftPath").toString();
    const QString rp = view->property("rightPath").toString();
    const QString l = dirDisplayName(lp);
    const QString r = dirDisplayName(rp);
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
        m_actNextDiff->setEnabled(n > 0 || dv->isAnyFileTruncated());
        m_actPrevDiff->setEnabled(n > 0);
        m_actNextDiffFile->setEnabled(true);
        m_actSave->setEnabled(dv->isDirty());
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
        m_actSave->setEnabled(false);
        for (int i = 0; i < m_filterCombo->count(); ++i) {
            if (m_filterCombo->itemData(i).toInt() == int(tv->filter())) {
                const QSignalBlocker blocker(m_filterCombo);
                m_filterCombo->setCurrentIndex(i);
                break;
            }
        }
        setWindowTitle(QString("twain — %1/ ⟷ %2/")
                           .arg(dirDisplayName(lp), dirDisplayName(rp)));
    } else {
        m_leftPathLabel->clear();
        m_rightPathLabel->clear();
        m_diffCountLabel->clear();
        m_actNextDiff->setEnabled(false);
        m_actPrevDiff->setEnabled(false);
        m_actNextDiffFile->setEnabled(false);
        m_actSave->setEnabled(false);
        setWindowTitle("twain");
    }
}

void MainWindow::onTabChanged(int /*index*/) {
    updateForCurrentTab();
    if (auto* tv = currentTreeView()) tv->focusTree();
}

void MainWindow::onTabCloseRequested(int index) {
    QWidget* w = m_tabs->widget(index);
    if (auto* dv = qobject_cast<DiffView*>(w); dv && dv->isDirty()) {
        const auto reply = QMessageBox::question(
            this, "twain",
            QString("Save changes to\n%1\n%2 ?")
                .arg(dv->leftPath(), dv->rightPath()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Save) {
            QString error;
            if (!dv->save(&error)) {
                QMessageBox::warning(this, "twain", error);
                return;
            }
        }
    }
    m_tabs->removeTab(index);
    if (w) w->deleteLater();
    updateForCurrentTab();
    if (m_tabs->count() == 0) close();
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
