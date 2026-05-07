#include "TreeCompareView.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QScrollBar>
#include <QSplitter>
#include <QTreeView>

#include "TreeCompareModel.h"

namespace {

class BranchedTreeView : public QTreeView {
public:
    using QTreeView::QTreeView;

protected:
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override {
        QTreeView::drawBranches(painter, rect, index);

        QVector<bool> hasSiblingBelow;
        QModelIndex idx = index;
        while (idx.isValid()) {
            const QModelIndex p = idx.parent();
            const bool hasSib = (idx.row() < model()->rowCount(p) - 1);
            hasSiblingBelow.prepend(hasSib);
            idx = p;
        }
        if (hasSiblingBelow.isEmpty()) return;

        painter->save();
        QPen pen(QColor(180, 180, 180));
        painter->setPen(pen);

        const int indent = indentation();

        for (int i = 0; i < hasSiblingBelow.size() - 1; ++i) {
            if (hasSiblingBelow[i]) {
                const int x = rect.left() + i * indent + indent / 2;
                painter->drawLine(x, rect.top(), x, rect.bottom());
            }
        }

        const int level = hasSiblingBelow.size() - 1;
        const int x = rect.left() + level * indent + indent / 2;
        const int yMid = rect.top() + rect.height() / 2;

        if (hasSiblingBelow[level]) {
            painter->drawLine(x, rect.top(), x, rect.bottom());
        } else {
            painter->drawLine(x, rect.top(), x, yMid);
        }
        painter->drawLine(x, yMid, x + indent / 2, yMid);

        painter->restore();
    }
};

}  // namespace

TreeCompareView::TreeCompareView(QWidget* parent) : QWidget(parent) {
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_left = new BranchedTreeView(m_splitter);
    m_right = new BranchedTreeView(m_splitter);
    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_right);
    m_splitter->setSizes({1, 1});

    m_model = new TreeCompareModel(this);
    m_left->setModel(m_model);
    m_right->setModel(m_model);

    for (QTreeView* tv : {m_left, m_right}) {
        tv->setUniformRowHeights(true);
        tv->setAlternatingRowColors(false);
        tv->setAllColumnsShowFocus(true);
        tv->setExpandsOnDoubleClick(false);
        tv->header()->setStretchLastSection(false);
    }
    m_left->hideColumn(TreeCompareModel::kColRightName);
    m_left->hideColumn(TreeCompareModel::kColRightSize);
    m_left->hideColumn(TreeCompareModel::kColRightMTime);
    m_right->hideColumn(TreeCompareModel::kColLeftName);
    m_right->hideColumn(TreeCompareModel::kColLeftSize);
    m_right->hideColumn(TreeCompareModel::kColLeftMTime);
    m_right->setTreePosition(TreeCompareModel::kColRightName);

    m_left->header()->setSectionResizeMode(TreeCompareModel::kColLeftName,
                                           QHeaderView::Stretch);
    m_left->header()->setSectionResizeMode(TreeCompareModel::kColLeftSize,
                                           QHeaderView::ResizeToContents);
    m_left->header()->setSectionResizeMode(TreeCompareModel::kColLeftMTime,
                                           QHeaderView::ResizeToContents);
    m_right->header()->setSectionResizeMode(TreeCompareModel::kColRightName,
                                            QHeaderView::Stretch);
    m_right->header()->setSectionResizeMode(TreeCompareModel::kColRightSize,
                                            QHeaderView::ResizeToContents);
    m_right->header()->setSectionResizeMode(TreeCompareModel::kColRightMTime,
                                            QHeaderView::ResizeToContents);

    m_right->setSelectionModel(m_left->selectionModel());

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);

    connect(m_left->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        m_right->verticalScrollBar()->setValue(v);
        m_syncingScroll = false;
    });
    connect(m_right->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (m_syncingScroll) return;
        m_syncingScroll = true;
        m_left->verticalScrollBar()->setValue(v);
        m_syncingScroll = false;
    });

    connect(m_left, &QTreeView::expanded, this, &TreeCompareView::onLeftExpanded);
    connect(m_left, &QTreeView::collapsed, this, &TreeCompareView::onLeftCollapsed);
    connect(m_right, &QTreeView::expanded, this, &TreeCompareView::onRightExpanded);
    connect(m_right, &QTreeView::collapsed, this, &TreeCompareView::onRightCollapsed);

    connect(m_left, &QTreeView::activated, this, &TreeCompareView::onDoubleClicked);
    connect(m_right, &QTreeView::activated, this, &TreeCompareView::onDoubleClicked);
}

void TreeCompareView::onLeftExpanded(const QModelIndex& index) {
    if (m_syncingExpand) return;
    m_syncingExpand = true;
    m_right->expand(index);
    m_syncingExpand = false;
}
void TreeCompareView::onLeftCollapsed(const QModelIndex& index) {
    if (m_syncingExpand) return;
    m_syncingExpand = true;
    m_right->collapse(index);
    m_syncingExpand = false;
}
void TreeCompareView::onRightExpanded(const QModelIndex& index) {
    if (m_syncingExpand) return;
    m_syncingExpand = true;
    m_left->expand(index);
    m_syncingExpand = false;
}
void TreeCompareView::onRightCollapsed(const QModelIndex& index) {
    if (m_syncingExpand) return;
    m_syncingExpand = true;
    m_left->collapse(index);
    m_syncingExpand = false;
}

void TreeCompareView::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    const auto* e = m_model->entryFor(index);
    if (!e) return;
    if (e->isDir) {
        QTreeView* sender = qobject_cast<QTreeView*>(QObject::sender());
        if (sender) {
            if (sender->isExpanded(index)) sender->collapse(index);
            else sender->expand(index);
        }
        return;
    }
    if (!e->leftPath.isEmpty() || !e->rightPath.isEmpty()) {
        emit fileActivated(e->leftPath, e->rightPath);
    }
}

bool TreeCompareView::setFolders(const QString& leftPath, const QString& rightPath, QString* error) {
    Q_UNUSED(error);
    auto root = TreeCompare::compare(leftPath, rightPath);
    m_model->setRoot(std::move(root));
    recountLeaves();
    for (int i = 0; i < TreeCompareModel::kColCount; ++i) m_left->resizeColumnToContents(i);
    for (int i = 0; i < TreeCompareModel::kColCount; ++i) m_right->resizeColumnToContents(i);
    m_left->expandAll();
    if (m_model->rowCount() > 0) {
        const QModelIndex first = m_model->index(0, 0);
        m_left->setCurrentIndex(first);
        m_left->scrollTo(first);
    }
    m_left->setFocus();
    emit comparisonUpdated();
    return true;
}

void TreeCompareView::setFilter(TreeCompareModel::FilterMode mode) {
    m_model->setFilter(mode);
    m_left->expandAll();
}

static QModelIndex nextDocOrderIndex(const QAbstractItemModel* model, const QModelIndex& curr) {
    if (curr.isValid() && model->rowCount(curr) > 0) {
        return model->index(0, 0, curr);
    }
    QModelIndex idx = curr;
    while (idx.isValid()) {
        const QModelIndex parent = idx.parent();
        const int row = idx.row();
        if (row + 1 < model->rowCount(parent)) {
            return model->index(row + 1, 0, parent);
        }
        idx = parent;
    }
    return {};
}

static QModelIndex prevDocOrderIndex(const QAbstractItemModel* model, const QModelIndex& curr) {
    if (!curr.isValid()) {
        const int rc = model->rowCount();
        if (rc == 0) return {};
        QModelIndex idx = model->index(rc - 1, 0);
        while (model->rowCount(idx) > 0) {
            idx = model->index(model->rowCount(idx) - 1, 0, idx);
        }
        return idx;
    }
    const int row = curr.row();
    const QModelIndex parent = curr.parent();
    if (row > 0) {
        QModelIndex prev = model->index(row - 1, 0, parent);
        while (model->rowCount(prev) > 0) {
            prev = model->index(model->rowCount(prev) - 1, 0, prev);
        }
        return prev;
    }
    return parent;
}

QPair<QString, QString> TreeCompareView::nextDifferentFile(bool open) {
    if (m_model->rowCount() == 0) return {};

    QModelIndex idx = nextDocOrderIndex(m_model, m_left->currentIndex());
    if (!idx.isValid()) return {};

    while (idx.isValid()) {
        const auto* e = m_model->entryFor(idx);
        if (e && !e->isDir && e->status != TreeCompare::Status::Same) {
            QModelIndex p = idx.parent();
            while (p.isValid()) {
                m_left->expand(p);
                p = p.parent();
            }
            m_left->setCurrentIndex(idx);
            m_left->scrollTo(idx);
            m_left->setFocus();
            if (open) emit fileActivated(e->leftPath, e->rightPath);
            return {e->leftPath, e->rightPath};
        }
        idx = nextDocOrderIndex(m_model, idx);
    }
    return {};
}

QPair<QString, QString> TreeCompareView::prevDifferentFile(bool open) {
    if (m_model->rowCount() == 0) return {};

    QModelIndex idx = prevDocOrderIndex(m_model, m_left->currentIndex());
    if (!idx.isValid()) return {};

    while (idx.isValid()) {
        const auto* e = m_model->entryFor(idx);
        if (e && !e->isDir && e->status != TreeCompare::Status::Same) {
            QModelIndex p = idx.parent();
            while (p.isValid()) {
                m_left->expand(p);
                p = p.parent();
            }
            m_left->setCurrentIndex(idx);
            m_left->scrollTo(idx);
            m_left->setFocus();
            if (open) emit fileActivated(e->leftPath, e->rightPath);
            return {e->leftPath, e->rightPath};
        }
        idx = prevDocOrderIndex(m_model, idx);
    }
    return {};
}

TreeCompareModel::FilterMode TreeCompareView::filter() const {
    return m_model->filter();
}

void TreeCompareView::recountLeaves() {
    m_sameCount = 0;
    m_differentCount = 0;
    m_leftOnlyCount = 0;
    m_rightOnlyCount = 0;
    recountWalk(m_model->rootEntry());
}

void TreeCompareView::recountWalk(const TreeCompare::Entry& entry) {
    if (!entry.isDir) {
        switch (entry.status) {
            case TreeCompare::Status::Same:      ++m_sameCount; break;
            case TreeCompare::Status::Different: ++m_differentCount; break;
            case TreeCompare::Status::LeftOnly:  ++m_leftOnlyCount; break;
            case TreeCompare::Status::RightOnly: ++m_rightOnlyCount; break;
        }
        return;
    }
    for (const auto& c : entry.children) recountWalk(c);
}

QByteArray TreeCompareView::saveSplitterState() const { return m_splitter->saveState(); }
void TreeCompareView::restoreSplitterState(const QByteArray& state) { m_splitter->restoreState(state); }
QByteArray TreeCompareView::saveHeaderState() const { return m_left->header()->saveState(); }
void TreeCompareView::restoreHeaderState(const QByteArray& state) {
    m_left->header()->restoreState(state);
    // Re-apply our resize modes; restoreState above may carry old modes.
    QHeaderView* l = m_left->header();
    QHeaderView* r = m_right->header();
    l->setStretchLastSection(false);
    r->setStretchLastSection(false);
    l->setSectionResizeMode(TreeCompareModel::kColLeftName, QHeaderView::Stretch);
    l->setSectionResizeMode(TreeCompareModel::kColLeftSize, QHeaderView::ResizeToContents);
    l->setSectionResizeMode(TreeCompareModel::kColLeftMTime, QHeaderView::ResizeToContents);
    r->setSectionResizeMode(TreeCompareModel::kColRightName, QHeaderView::Stretch);
    r->setSectionResizeMode(TreeCompareModel::kColRightSize, QHeaderView::ResizeToContents);
    r->setSectionResizeMode(TreeCompareModel::kColRightMTime, QHeaderView::ResizeToContents);
}
