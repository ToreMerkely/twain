#include "TreeCompareModel.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QLocale>

namespace {

QString formatSize(qint64 bytes) {
    if (bytes < 0) return QString();
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString formatMTime(const QDateTime& dt) {
    if (!dt.isValid()) return QString();
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}

QColor foregroundFor(TreeCompare::Status s, bool leftSide) {
    switch (s) {
        case TreeCompare::Status::Different: return QColor(200, 0, 0);
        case TreeCompare::Status::LeftOnly:  return leftSide ? QColor(0, 80, 200) : QColor();
        case TreeCompare::Status::RightOnly: return leftSide ? QColor() : QColor(0, 80, 200);
        case TreeCompare::Status::Same:
        default:                             return QColor();
    }
}

}  // namespace

TreeCompareModel::TreeCompareModel(QObject* parent)
    : QAbstractItemModel(parent), m_iconProvider(new QFileIconProvider()) {}

TreeCompareModel::~TreeCompareModel() { delete m_iconProvider; }

void TreeCompareModel::setRoot(TreeCompare::Entry root) {
    beginResetModel();
    m_root = std::move(root);
    m_root.parent = nullptr;
    linkParents(m_root);
    rebuildVisibility();
    endResetModel();
}

void TreeCompareModel::setFilter(FilterMode mode) {
    if (m_filter == mode) return;
    beginResetModel();
    m_filter = mode;
    rebuildVisibility();
    endResetModel();
}

bool TreeCompareModel::computeVisibility(const TreeCompare::Entry& entry) {
    QVector<int> visible;
    bool selfVisible = false;
    if (entry.isDir) {
        for (int i = 0; i < entry.children.size(); ++i) {
            const auto& c = entry.children[i];
            if (computeVisibility(c)) {
                visible.append(i);
                selfVisible = true;
            }
        }
        m_visibleChildren.insert(&entry, visible);
        if (&entry == &m_root) return selfVisible;
        // A folder itself shows iff it has visible descendants.
        return selfVisible;
    }
    // Leaf
    switch (m_filter) {
        case FilterMode::All: return true;
        case FilterMode::DifferencesOnly:
            return entry.status != TreeCompare::Status::Same;
        case FilterMode::LeftOnly:
            return entry.status == TreeCompare::Status::LeftOnly;
        case FilterMode::RightOnly:
            return entry.status == TreeCompare::Status::RightOnly;
    }
    return true;
}

void TreeCompareModel::rebuildVisibility() {
    m_visibleChildren.clear();
    computeVisibility(m_root);
}

void TreeCompareModel::linkParents(TreeCompare::Entry& entry) {
    for (auto& child : entry.children) {
        child.parent = &entry;
        linkParents(child);
    }
}

void TreeCompareModel::notifyDeepCompareDone() {
    if (m_root.children.isEmpty()) return;
    beginResetModel();
    rebuildVisibility();
    endResetModel();
}

const TreeCompare::Entry* TreeCompareModel::entryFor(const QModelIndex& index) const {
    if (!index.isValid()) return &m_root;
    return static_cast<const TreeCompare::Entry*>(index.internalPointer());
}

int TreeCompareModel::rowCount(const QModelIndex& parent) const {
    const TreeCompare::Entry* e = entryFor(parent);
    if (!e) return 0;
    if (parent.isValid() && parent.column() != 0) return 0;
    auto it = m_visibleChildren.find(e);
    if (it == m_visibleChildren.end()) return 0;
    return it.value().size();
}

int TreeCompareModel::columnCount(const QModelIndex& /*parent*/) const { return kColCount; }

QModelIndex TreeCompareModel::index(int row, int column, const QModelIndex& parent) const {
    if (row < 0 || column < 0 || column >= kColCount) return {};
    const TreeCompare::Entry* parentEntry = entryFor(parent);
    if (!parentEntry) return {};
    auto it = m_visibleChildren.find(parentEntry);
    if (it == m_visibleChildren.end() || row >= it.value().size()) return {};
    const int realIdx = it.value()[row];
    const TreeCompare::Entry* child = &parentEntry->children[realIdx];
    return createIndex(row, column, const_cast<TreeCompare::Entry*>(child));
}

QModelIndex TreeCompareModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    const auto* childEntry = static_cast<const TreeCompare::Entry*>(child.internalPointer());
    if (!childEntry || !childEntry->parent || childEntry->parent == &m_root) return {};
    const TreeCompare::Entry* parentEntry = childEntry->parent;
    const TreeCompare::Entry* grand = parentEntry->parent;
    if (!grand) return {};
    auto it = m_visibleChildren.find(grand);
    if (it == m_visibleChildren.end()) return {};
    const auto& visible = it.value();
    int row = -1;
    for (int i = 0; i < visible.size(); ++i) {
        if (&grand->children[visible[i]] == parentEntry) { row = i; break; }
    }
    if (row < 0) return {};
    return createIndex(row, 0, const_cast<TreeCompare::Entry*>(parentEntry));
}

QVariant TreeCompareModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto* e = static_cast<const TreeCompare::Entry*>(index.internalPointer());
    if (!e) return {};

    const int col = index.column();
    const bool leftSide = col < kColRightName;
    const bool hasLeft = !e->leftPath.isEmpty();
    const bool hasRight = !e->rightPath.isEmpty();
    const bool sideHasEntry = leftSide ? hasLeft : hasRight;

    switch (role) {
        case Qt::DisplayRole: {
            if (!sideHasEntry) return {};
            switch (col) {
                case kColLeftName:
                case kColRightName: return e->name;
                case kColLeftSize:  return e->isDir ? QString() : formatSize(e->leftSize);
                case kColRightSize: return e->isDir ? QString() : formatSize(e->rightSize);
                case kColLeftMTime: return formatMTime(e->leftMTime);
                case kColRightMTime: return formatMTime(e->rightMTime);
            }
            return {};
        }
        case Qt::DecorationRole:
            if ((col == kColLeftName || col == kColRightName) && sideHasEntry) {
                return e->isDir ? m_iconProvider->icon(QFileIconProvider::Folder)
                                : m_iconProvider->icon(QFileIconProvider::File);
            }
            return {};
        case Qt::ForegroundRole: {
            if (!sideHasEntry) return {};
            if (e->isDir) return {};
            QColor c = foregroundFor(e->status, leftSide);
            if (!c.isValid()) return {};
            return QBrush(c);
        }
        case Qt::FontRole:
            return {};
    }
    return {};
}

QVariant TreeCompareModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case kColLeftName:   return "Name";
        case kColLeftSize:   return "Size";
        case kColLeftMTime:  return "Modified";
        case kColRightName:  return "Name";
        case kColRightSize:  return "Size";
        case kColRightMTime: return "Modified";
    }
    return {};
}
