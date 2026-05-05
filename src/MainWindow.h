#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class DiffView;
class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QTabWidget;
class TreeCompareView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadPair(const QString& leftPath, const QString& rightPath);
    void loadFolderPair(const QString& leftPath, const QString& rightPath);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void openPair();
    void openLeft();
    void openRight();
    void openFolderPair();
    void refresh();
    void save();
    void undo();
    void nextDifference();
    void prevDifference();
    void nextDifferentFile();
    void showAbout();
    void showGitDiffToolHelp();
    void rebuildRecentMenu();
    void rebuildRecentFoldersMenu();
    void onFileActivatedFromTree(const QString& leftPath, const QString& rightPath);
    void onFolderComparisonUpdated();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void updatePathStatus();
    void updateFolderPathStatus();
    void tryLoadPair();
    void readSettings();
    void writeSettings();
    void rememberRecentPair(const QString& prefix, const QString& left, const QString& right);

    QString m_leftPath;
    QString m_rightPath;

    QAction* m_actOpenPair = nullptr;
    QAction* m_actOpenLeft = nullptr;
    QAction* m_actOpenRight = nullptr;
    QAction* m_actOpenFolderPair = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actQuit = nullptr;
    QAction* m_actNextDiff = nullptr;
    QAction* m_actPrevDiff = nullptr;
    QAction* m_actNextDiffFile = nullptr;
    QAction* m_actCloseTab = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actIgnoreCase = nullptr;
    QAction* m_actIgnoreWhitespace = nullptr;
    QAction* m_actIgnoreBlankLines = nullptr;
    QAction* m_actAbout = nullptr;
    QAction* m_actGitDifftool = nullptr;

    QMenu* m_recentMenu = nullptr;
    QMenu* m_recentFoldersMenu = nullptr;

    QLabel* m_leftPathLabel = nullptr;
    QLabel* m_rightPathLabel = nullptr;
    QLabel* m_diffCountLabel = nullptr;

    QTabWidget* m_tabs = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QAction* m_filterComboAction = nullptr;

    DiffView* currentDiffView() const;
    TreeCompareView* currentTreeView() const;
    void applyDiffOptionsToAllTabs();
    DiffView* findDiffTabForPair(const QString& left, const QString& right) const;
    TreeCompareView* findTreeTabForPair(const QString& left, const QString& right) const;
    DiffView* createDiffTab(const QString& left, const QString& right);
    TreeCompareView* createTreeTab(const QString& left, const QString& right);
    void updateDiffTabTitle(DiffView* view);
    void updateTreeTabTitle(TreeCompareView* view);
    void updateForCurrentTab();
};
