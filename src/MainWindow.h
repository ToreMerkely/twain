#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class DiffView;
class QAction;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadPair(const QString& leftPath, const QString& rightPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openPair();
    void openLeft();
    void openRight();
    void refresh();
    void nextDifference();
    void prevDifference();
    void showAbout();
    void rebuildRecentMenu();

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void updatePathStatus();
    void tryLoadPair();
    void readSettings();
    void writeSettings();
    void rememberRecent(const QString& left, const QString& right);

    QString m_leftPath;
    QString m_rightPath;

    QAction* m_actOpenPair = nullptr;
    QAction* m_actOpenLeft = nullptr;
    QAction* m_actOpenRight = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actQuit = nullptr;
    QAction* m_actNextDiff = nullptr;
    QAction* m_actPrevDiff = nullptr;
    QAction* m_actAbout = nullptr;

    QMenu* m_recentMenu = nullptr;

    QLabel* m_leftPathLabel = nullptr;
    QLabel* m_rightPathLabel = nullptr;
    QLabel* m_diffCountLabel = nullptr;

    DiffView* m_diffView = nullptr;
};
