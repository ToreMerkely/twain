#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QVector>

#include "TreeCompare.h"

class QFileIconProvider;

class TreeCompareModel : public QAbstractItemModel {
    Q_OBJECT
public:
    static constexpr int kColLeftName = 0;
    static constexpr int kColLeftSize = 1;
    static constexpr int kColLeftMTime = 2;
    static constexpr int kColRightName = 3;
    static constexpr int kColRightSize = 4;
    static constexpr int kColRightMTime = 5;
    static constexpr int kColCount = 6;

    enum class FilterMode { All, DifferencesOnly, LeftOnly, RightOnly };

    explicit TreeCompareModel(QObject* parent = nullptr);
    ~TreeCompareModel() override;

    void setRoot(TreeCompare::Entry root);
    TreeCompare::Entry& rootEntry() { return m_root; }
    const TreeCompare::Entry* entryFor(const QModelIndex& index) const;

    void setFilter(FilterMode mode);
    FilterMode filter() const { return m_filter; }

    void notifyDeepCompareDone();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    TreeCompare::Entry m_root;
    QFileIconProvider* m_iconProvider;
    FilterMode m_filter = FilterMode::All;
    QHash<const TreeCompare::Entry*, QVector<int>> m_visibleChildren;

    void linkParents(TreeCompare::Entry& entry);
    bool computeVisibility(const TreeCompare::Entry& entry);
    void rebuildVisibility();
};
