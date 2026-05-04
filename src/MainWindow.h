#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class DiffView;
class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QStackedWidget;
class TreeCompareView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class Mode { File, Folder };

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadPair(const QString& leftPath, const QString& rightPath);
    void loadFolderPair(const QString& leftPath, const QString& rightPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openPair();
    void openLeft();
    void openRight();
    void openFolderPair();
    void refresh();
    void nextDifference();
    void prevDifference();
    void back();
    void showAbout();
    void rebuildRecentMenu();
    void rebuildRecentFoldersMenu();
    void onFileActivatedFromTree(const QString& leftPath, const QString& rightPath);
    void onFolderComparisonUpdated();

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
    void setMode(Mode m);

    Mode m_mode = Mode::File;
    QString m_leftPath;
    QString m_rightPath;
    QString m_leftFolder;
    QString m_rightFolder;
    QString m_lastFolderPairLeft;
    QString m_lastFolderPairRight;

    QAction* m_actOpenPair = nullptr;
    QAction* m_actOpenLeft = nullptr;
    QAction* m_actOpenRight = nullptr;
    QAction* m_actOpenFolderPair = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actQuit = nullptr;
    QAction* m_actNextDiff = nullptr;
    QAction* m_actPrevDiff = nullptr;
    QAction* m_actBack = nullptr;
    QAction* m_actAbout = nullptr;

    QMenu* m_recentMenu = nullptr;
    QMenu* m_recentFoldersMenu = nullptr;

    QLabel* m_leftPathLabel = nullptr;
    QLabel* m_rightPathLabel = nullptr;
    QLabel* m_diffCountLabel = nullptr;

    QStackedWidget* m_stack = nullptr;
    DiffView* m_diffView = nullptr;
    TreeCompareView* m_treeView = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QAction* m_filterComboAction = nullptr;
};
