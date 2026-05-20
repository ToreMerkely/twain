#pragma once

#include <QByteArray>
#include <QModelIndex>
#include <QString>
#include <QWidget>

#include <QPair>

#include "TreeCompare.h"
#include "TreeCompareModel.h"

class QSplitter;
class QTreeView;

class TreeCompareView : public QWidget {
    Q_OBJECT
public:
    explicit TreeCompareView(QWidget* parent = nullptr);

    bool setFolders(const QString& leftPath, const QString& rightPath, QString* error = nullptr);

    void setFilter(TreeCompareModel::FilterMode mode);
    TreeCompareModel::FilterMode filter() const;

    QPair<QString, QString> nextDifferentFile(bool open = true);
    QPair<QString, QString> prevDifferentFile(bool open = true);

    int sameCount() const { return m_sameCount; }
    int differentCount() const { return m_differentCount; }
    int leftOnlyCount() const { return m_leftOnlyCount; }
    int rightOnlyCount() const { return m_rightOnlyCount; }

    QByteArray saveSplitterState() const;
    void restoreSplitterState(const QByteArray& state);
    QByteArray saveHeaderState() const;
    void restoreHeaderState(const QByteArray& state);

    void focusTree();

    // Apply the current diff font size (managed by DiffView) to both trees.
    void applyDiffFontSize();

signals:
    void fileActivated(const QString& leftPath, const QString& rightPath);
    void comparisonUpdated();

private slots:
    void onLeftExpanded(const QModelIndex& index);
    void onLeftCollapsed(const QModelIndex& index);
    void onRightExpanded(const QModelIndex& index);
    void onRightCollapsed(const QModelIndex& index);
    void onDoubleClicked(const QModelIndex& index);

private:
    void recountLeaves();
    void recountWalk(const TreeCompare::Entry& entry);

    QSplitter* m_splitter;
    QTreeView* m_left;
    QTreeView* m_right;
    TreeCompareModel* m_model;

    bool m_syncingScroll = false;
    bool m_syncingExpand = false;

    int m_sameCount = 0;
    int m_differentCount = 0;
    int m_leftOnlyCount = 0;
    int m_rightOnlyCount = 0;
};
