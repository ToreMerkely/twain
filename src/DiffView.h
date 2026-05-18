#pragma once

#include <QByteArray>
#include <QSet>
#include <QStack>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class DiffOverview;
class DiffPane;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSplitter;

class DiffView : public QWidget {
    Q_OBJECT
public:
    explicit DiffView(QWidget* parent = nullptr);

    struct Options {
        bool ignoreCase = false;
        bool ignoreWhitespace = false;
        bool ignoreBlankLines = false;
    };

    bool setFiles(const QString& leftPath, const QString& rightPath, QString* error = nullptr);
    void setOptions(Options opts);
    Options options() const { return m_options; }
    int differenceCount() const { return m_diffBlocks.size(); }
    int currentDifference() const { return m_currentDiff; }
    bool isAnyFileTruncated() const {
        return m_leftLoadInfo.truncated || m_rightLoadInfo.truncated;
    }
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    bool isDirty() const { return m_dirty; }
    bool save(QString* error = nullptr);

    bool canUndoArrow() const { return !m_arrowUndoStack.isEmpty(); }
    void undoArrow();

    void nextDifference();
    void prevDifference();

    QByteArray saveSplitterState() const;
    void restoreSplitterState(const QByteArray& state);

    void showSearchBar();

    struct Block {
        int rowStart = 0;
        int rowEnd = 0;
        int leftStart = 0;
        int leftCount = 0;
        int rightStart = 0;
        int rightCount = 0;
    };

    struct FileLoadInfo {
        bool truncated = false;
        qint64 totalBytes = 0;
        int totalLines = 0;
        qint64 streamOffset = 0;
        QStringList pendingLines;
    };

signals:
    void currentDifferenceChanged(int index, int total);
    void dirtyChanged(bool dirty);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QSplitter* m_splitter;
    DiffPane* m_left;
    DiffPane* m_right;
    DiffOverview* m_overview = nullptr;
    QLabel* m_leftPathLabel = nullptr;
    QLabel* m_rightPathLabel = nullptr;
    QWidget* m_searchBar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchStatus = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    bool m_ignoreNextWatch = false;
    QPlainTextEdit* m_currentLeftLine = nullptr;
    QPlainTextEdit* m_currentRightLine = nullptr;
    bool m_syncing = false;

    QStringList m_leftLines;
    QStringList m_rightLines;
    FileLoadInfo m_leftLoadInfo;
    FileLoadInfo m_rightLoadInfo;
    struct ArrowSnapshot {
        QStringList leftLines;
        QStringList rightLines;
    };
    QStack<ArrowSnapshot> m_arrowUndoStack;
    QVector<Block> m_diffBlocks;
    int m_highlightLeftStart = -1;
    int m_highlightLeftCount = 0;
    int m_highlightRightStart = -1;
    int m_highlightRightCount = 0;
    int m_currentDiff = -1;
    QString m_leftPath;
    QString m_rightPath;
    Options m_options;
    bool m_dirty = false;

    int m_partialBlockIdx = -1;
    bool m_partialFromLeftPane = true;
    int m_partialAnchorRow = -1;
    QSet<int> m_partialRows;

    void rebuildView();
    void loadMore();
    void scrollToBottom();
    void onArrowClicked(bool fromLeftPane, int row);
    void onLineNumberClicked(bool fromLeftPane, int row, bool shift);
    DiffPane* currentSearchTarget() const;
    void updateSearchStatus();
    void clearPartialSelection();
    void applyPartialVisuals();
    void updateCurrentLineDisplay(DiffPane* source);
    void setDirty(bool d);

    void syncScroll(DiffPane* source, DiffPane* target, int value);
    void goToDiff(int index);
    int blockIndexAtRow(int row) const;
};
