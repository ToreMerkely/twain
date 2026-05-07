#include "DiffView.h"

#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>

#include "Diff.h"
#include "DiffPane.h"

DiffView::DiffView(QWidget* parent) : QWidget(parent) {
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_left = new DiffPane(m_splitter);
    m_right = new DiffPane(m_splitter);
    m_left->setSide(DiffPane::Side::Left);
    m_right->setSide(DiffPane::Side::Right);
    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_right);
    m_splitter->setSizes({1, 1});

    QFont monoFont("JetBrains Mono", 10);
    monoFont.setStyleHint(QFont::Monospace);
    const int oneLineHeight = QFontMetrics(monoFont).height() + 4;
    auto makeLineWidget = [&]() {
        auto* w = new QPlainTextEdit(this);
        w->setReadOnly(true);
        w->setFont(monoFont);
        w->setFocusPolicy(Qt::NoFocus);
        w->setLineWrapMode(QPlainTextEdit::NoWrap);
        w->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        w->setFixedHeight(oneLineHeight);
        w->setFrameStyle(QFrame::NoFrame);
        return w;
    };
    m_currentLeftLine = makeLineWidget();
    m_currentRightLine = makeLineWidget();

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(2, 2, 2, 2);
    bottomLayout->setSpacing(0);
    bottomLayout->addWidget(m_currentLeftLine);
    bottomLayout->addWidget(m_currentRightLine);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(bottom, 0);

    connect(m_left->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int v) { syncScroll(m_left, m_right, v); });
    connect(m_right->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int v) { syncScroll(m_right, m_left, v); });
    connect(m_left->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int v) {
                if (m_syncing) return;
                m_syncing = true;
                m_right->horizontalScrollBar()->setValue(v);
                m_syncing = false;
            });
    connect(m_right->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int v) {
                if (m_syncing) return;
                m_syncing = true;
                m_left->horizontalScrollBar()->setValue(v);
                m_syncing = false;
            });
    connect(m_left, &DiffPane::arrowClicked, this,
            [this](int row) { onArrowClicked(/*fromLeftPane=*/true, row); });
    connect(m_right, &DiffPane::arrowClicked, this,
            [this](int row) { onArrowClicked(/*fromLeftPane=*/false, row); });
    connect(m_left, &DiffPane::lineNumberClicked, this,
            [this](int row, bool shift) { onLineNumberClicked(/*fromLeftPane=*/true, row, shift); });
    connect(m_right, &DiffPane::lineNumberClicked, this,
            [this](int row, bool shift) { onLineNumberClicked(/*fromLeftPane=*/false, row, shift); });
    connect(m_left, &DiffPane::clearPartialRequested, this, &DiffView::clearPartialSelection);
    connect(m_right, &DiffPane::clearPartialRequested, this, &DiffView::clearPartialSelection);
    connect(m_left, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateCurrentLineDisplay(m_left); });
    connect(m_right, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateCurrentLineDisplay(m_right); });
    connect(m_left, &DiffPane::contentEdited, this, [this]() {
        const QStringList latest = m_left->extractContent();
        if (latest == m_leftLines) return;
        m_leftLines = latest;
        setDirty(true);
    });
    connect(m_right, &DiffPane::contentEdited, this, [this]() {
        const QStringList latest = m_right->extractContent();
        if (latest == m_rightLines) return;
        m_rightLines = latest;
        setDirty(true);
    });
}

QByteArray DiffView::saveSplitterState() const { return m_splitter->saveState(); }
void DiffView::restoreSplitterState(const QByteArray& state) { m_splitter->restoreState(state); }

void DiffView::syncScroll(DiffPane* source, DiffPane* target, int value) {
    if (m_syncing) return;
    m_syncing = true;
    target->verticalScrollBar()->setValue(value);
    m_syncing = false;
    Q_UNUSED(source);
}

static bool looksBinary(const QByteArray& bytes) {
    const int probe = qMin<int>(bytes.size(), 8192);
    for (int i = 0; i < probe; ++i) {
        if (bytes[i] == '\0') return true;
    }
    return false;
}

static QStringList readFileLines(const QString& path, bool* ok, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        if (err) *err = "Could not open " + path;
        return {};
    }
    const QByteArray bytes = f.readAll();
    if (looksBinary(bytes)) {
        if (ok) *ok = false;
        if (err) *err = path + " appears to be a binary file";
        return {};
    }
    QString text = QString::fromUtf8(bytes);
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    QStringList lines = text.split('\n');
    // split() leaves a trailing empty element when text ends with '\n' — drop it
    if (!lines.isEmpty() && lines.last().isEmpty() && text.endsWith('\n')) {
        lines.removeLast();
    }
    if (ok) *ok = true;
    return lines;
}

static void filterBlanks(const QStringList& orig, QStringList* out, QVector<int>* origIdx) {
    out->clear();
    origIdx->clear();
    out->reserve(orig.size());
    origIdx->reserve(orig.size());
    for (int i = 0; i < orig.size(); ++i) {
        if (!orig[i].trimmed().isEmpty()) {
            out->append(orig[i]);
            origIdx->append(i);
        }
    }
}

struct AlignedBuildResult {
    QVector<DiffView::Block> blocks;
};

struct HighlightRange {
    int leftStart = -1;
    int leftCount = 0;
    int rightStart = -1;
    int rightCount = 0;
};

static bool inRange(int idx, int start, int count) {
    return start >= 0 && idx >= start && idx < start + count;
}

static AlignedBuildResult buildAlignedRows(
    const QStringList& left, const QStringList& right,
    const QVector<int>& leftMap, const QVector<int>& rightMap,
    const QVector<Diff::Hunk>& hunks, Diff::Options diffOpts,
    HighlightRange highlight,
    QVector<DiffRow>& leftRows, QVector<DiffRow>& rightRows) {
    AlignedBuildResult res;
    leftRows.clear();
    rightRows.clear();

    int i = 0;
    while (i < hunks.size()) {
        const auto& h = hunks[i];
        if (h.op == Diff::Op::Equal) {
            for (int k = 0; k < h.leftCount; ++k) {
                const int li = leftMap[h.leftStart + k];
                const int ri = rightMap[h.rightStart + k];
                DiffRow lr{li, left[li], Diff::Op::Equal, false, {}, false};
                DiffRow rr{ri, right[ri], Diff::Op::Equal, false, {}, false};
                lr.recentlyCopied = inRange(li, highlight.leftStart, highlight.leftCount);
                rr.recentlyCopied = inRange(ri, highlight.rightStart, highlight.rightCount);
                leftRows.append(lr);
                rightRows.append(rr);
            }
            ++i;
            continue;
        }

        // Pair adjacent Delete + Insert as a single change block.
        Diff::Hunk del{Diff::Op::Delete, 0, 0, 0, 0};
        Diff::Hunk ins{Diff::Op::Insert, 0, 0, 0, 0};
        bool hasDel = false, hasIns = false;
        if (h.op == Diff::Op::Delete) {
            del = h;
            hasDel = true;
            if (i + 1 < hunks.size() && hunks[i + 1].op == Diff::Op::Insert) {
                ins = hunks[i + 1];
                hasIns = true;
                i += 2;
            } else {
                ++i;
            }
        } else {  // Insert
            ins = h;
            hasIns = true;
            ++i;
        }

        DiffView::Block block;
        block.rowStart = leftRows.size();
        block.leftCount = hasDel ? del.leftCount : 0;
        block.rightCount = hasIns ? ins.rightCount : 0;
        if (hasDel) {
            const int firstOrig = leftMap[del.leftStart];
            const int lastOrig = leftMap[del.leftStart + del.leftCount - 1];
            block.leftStart = firstOrig;
            block.leftCount = lastOrig - firstOrig + 1;
        } else {
            block.leftStart = ins.leftStart < leftMap.size()
                                  ? leftMap[ins.leftStart]
                                  : left.size();
            block.leftCount = 0;
        }
        if (hasIns) {
            const int firstOrig = rightMap[ins.rightStart];
            const int lastOrig = rightMap[ins.rightStart + ins.rightCount - 1];
            block.rightStart = firstOrig;
            block.rightCount = lastOrig - firstOrig + 1;
        } else {
            block.rightStart = del.rightStart < rightMap.size()
                                   ? rightMap[del.rightStart]
                                   : right.size();
            block.rightCount = 0;
        }

        // Build line lists (in original-line order) for the alignment pass.
        QStringList leftBlock;
        QStringList rightBlock;
        QVector<int> leftBlockOrigIdx;
        QVector<int> rightBlockOrigIdx;
        if (hasDel) {
            for (int k = 0; k < del.leftCount; ++k) {
                const int li = leftMap[del.leftStart + k];
                leftBlock.append(left[li]);
                leftBlockOrigIdx.append(li);
            }
        }
        if (hasIns) {
            for (int k = 0; k < ins.rightCount; ++k) {
                const int ri = rightMap[ins.rightStart + k];
                rightBlock.append(right[ri]);
                rightBlockOrigIdx.append(ri);
            }
        }

        QVector<Diff::AlignmentPair> pairs;
        if (hasDel && hasIns) {
            pairs = Diff::alignBlock(leftBlock, rightBlock);
        } else {
            for (int k = 0; k < leftBlock.size(); ++k) pairs.append({k, -1});
            for (int k = 0; k < rightBlock.size(); ++k) pairs.append({-1, k});
        }

        for (const auto& p : pairs) {
            DiffRow lr;
            DiffRow rr;
            if (p.leftIdx >= 0) {
                const int li = leftBlockOrigIdx[p.leftIdx];
                lr = {li, leftBlock[p.leftIdx], Diff::Op::Delete, false, {}, false};
            } else {
                lr = {-1, QString(), Diff::Op::Delete, true, {}, false};
            }
            if (p.rightIdx >= 0) {
                const int ri = rightBlockOrigIdx[p.rightIdx];
                rr = {ri, rightBlock[p.rightIdx], Diff::Op::Insert, false, {}, false};
            } else {
                rr = {-1, QString(), Diff::Op::Insert, true, {}, false};
            }
            if (p.leftIdx >= 0) {
                lr.recentlyCopied = inRange(lr.sourceLine, highlight.leftStart, highlight.leftCount);
            }
            if (p.rightIdx >= 0) {
                rr.recentlyCopied = inRange(rr.sourceLine, highlight.rightStart, highlight.rightCount);
            }
            if (p.leftIdx >= 0 && p.rightIdx >= 0) {
                const auto ld = Diff::lineDiff(lr.text, rr.text, diffOpts);
                lr.segments = ld.left;
                rr.segments = ld.right;
            }
            leftRows.append(lr);
            rightRows.append(rr);
        }
        block.rowEnd = leftRows.size();
        res.blocks.append(block);
    }
    return res;
}

bool DiffView::setFiles(const QString& leftPath, const QString& rightPath, QString* error) {
    QStringList leftLines, rightLines;
    if (!leftPath.isEmpty()) {
        bool okL = false;
        QString errL;
        leftLines = readFileLines(leftPath, &okL, &errL);
        if (!okL) {
            if (error) *error = errL;
            return false;
        }
    }
    if (!rightPath.isEmpty()) {
        bool okR = false;
        QString errR;
        rightLines = readFileLines(rightPath, &okR, &errR);
        if (!okR) {
            if (error) *error = errR;
            return false;
        }
    }

    Diff::Options diffOpts;
    diffOpts.ignoreCase = m_options.ignoreCase;
    diffOpts.ignoreWhitespace = m_options.ignoreWhitespace;

    QStringList compareLeft = leftLines;
    QStringList compareRight = rightLines;
    QVector<int> leftMap, rightMap;
    if (m_options.ignoreBlankLines) {
        filterBlanks(leftLines, &compareLeft, &leftMap);
        filterBlanks(rightLines, &compareRight, &rightMap);
    } else {
        leftMap.reserve(leftLines.size());
        rightMap.reserve(rightLines.size());
        for (int i = 0; i < leftLines.size(); ++i) leftMap.append(i);
        for (int i = 0; i < rightLines.size(); ++i) rightMap.append(i);
    }

    m_leftPath = leftPath;
    m_rightPath = rightPath;
    m_leftLines = leftLines;
    m_rightLines = rightLines;
    m_left->setLanguageFromPath(leftPath.isEmpty() ? rightPath : leftPath);
    m_right->setLanguageFromPath(rightPath.isEmpty() ? leftPath : rightPath);

    rebuildView();
    setDirty(false);

    m_currentDiff = -1;
    m_left->verticalScrollBar()->setValue(0);
    m_right->verticalScrollBar()->setValue(0);
    QTextCursor c(m_left->document()->findBlockByNumber(0));
    m_left->setTextCursor(c);
    m_left->setFocus();
    emit currentDifferenceChanged(-1, m_diffBlocks.size());
    return true;
}

void DiffView::rebuildView() {
    m_partialBlockIdx = -1;
    m_partialRows.clear();
    m_partialAnchorRow = -1;
    Diff::Options diffOpts;
    diffOpts.ignoreCase = m_options.ignoreCase;
    diffOpts.ignoreWhitespace = m_options.ignoreWhitespace;

    QStringList compareLeft = m_leftLines;
    QStringList compareRight = m_rightLines;
    QVector<int> leftMap, rightMap;
    if (m_options.ignoreBlankLines) {
        filterBlanks(m_leftLines, &compareLeft, &leftMap);
        filterBlanks(m_rightLines, &compareRight, &rightMap);
    } else {
        leftMap.reserve(m_leftLines.size());
        rightMap.reserve(m_rightLines.size());
        for (int i = 0; i < m_leftLines.size(); ++i) leftMap.append(i);
        for (int i = 0; i < m_rightLines.size(); ++i) rightMap.append(i);
    }

    const auto hunks = Diff::compute(compareLeft, compareRight, diffOpts);
    QVector<DiffRow> leftRows, rightRows;
    HighlightRange hl;
    hl.leftStart = m_highlightLeftStart;
    hl.leftCount = m_highlightLeftCount;
    hl.rightStart = m_highlightRightStart;
    hl.rightCount = m_highlightRightCount;
    auto res = buildAlignedRows(m_leftLines, m_rightLines, leftMap, rightMap,
                                hunks, diffOpts, hl, leftRows, rightRows);
    m_diffBlocks = res.blocks;
    m_left->setRows(leftRows);
    m_right->setRows(rightRows);
}

void DiffView::setOptions(Options opts) {
    m_options = opts;
    if (!m_leftPath.isEmpty() || !m_rightPath.isEmpty()) {
        setFiles(m_leftPath, m_rightPath);
    }
}

void DiffView::nextDifference() {
    if (m_diffBlocks.isEmpty()) return;
    if (m_currentDiff + 1 >= m_diffBlocks.size()) return;
    goToDiff(m_currentDiff + 1);
}

void DiffView::prevDifference() {
    if (m_diffBlocks.isEmpty()) return;
    if (m_currentDiff <= 0) return;
    goToDiff(m_currentDiff - 1);
}

void DiffView::goToDiff(int index) {
    if (index < 0 || index >= m_diffBlocks.size()) return;
    m_currentDiff = index;
    const int row = m_diffBlocks[index].rowStart;

    QTextCursor cursor(m_left->document()->findBlockByNumber(row));
    m_left->setTextCursor(cursor);
    m_left->centerCursor();
    m_left->setFocus();
    m_syncing = true;
    m_right->verticalScrollBar()->setValue(m_left->verticalScrollBar()->value());
    m_syncing = false;

    emit currentDifferenceChanged(m_currentDiff, m_diffBlocks.size());
}

int DiffView::blockIndexAtRow(int row) const {
    for (int i = 0; i < m_diffBlocks.size(); ++i) {
        const auto& b = m_diffBlocks[i];
        if (row >= b.rowStart && row < b.rowEnd) return i;
    }
    return -1;
}

void DiffView::updateCurrentLineDisplay(DiffPane* source) {
    const int row = source->textCursor().blockNumber();
    if (row < 0) {
        m_currentLeftLine->clear();
        m_currentRightLine->clear();
        return;
    }
    const QString lt = m_left->document()->findBlockByNumber(row).text();
    const QString rt = m_right->document()->findBlockByNumber(row).text();
    m_currentLeftLine->setPlainText(lt);
    m_currentRightLine->setPlainText(rt);

    Diff::Options opts;
    opts.ignoreCase = m_options.ignoreCase;
    opts.ignoreWhitespace = m_options.ignoreWhitespace;
    const auto ld = Diff::lineDiff(lt, rt, opts);

    auto applySegs = [](QPlainTextEdit* pte, const QVector<Diff::LineSegment>& segs) {
        QList<QTextEdit::ExtraSelection> sels;
        QTextBlock blk = pte->document()->firstBlock();
        if (!blk.isValid()) {
            pte->setExtraSelections(sels);
            return;
        }
        for (const auto& s : segs) {
            if (!s.differ) continue;
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(QColor(255, 150, 150));
            QTextCursor c(blk);
            const int start = blk.position() + s.start;
            const int end = start + s.length;
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            sel.cursor = c;
            sels.append(sel);
        }
        pte->setExtraSelections(sels);
    };
    applySegs(m_currentLeftLine, ld.left);
    applySegs(m_currentRightLine, ld.right);
}

void DiffView::clearPartialSelection() {
    m_partialBlockIdx = -1;
    m_partialRows.clear();
    m_partialAnchorRow = -1;
    m_left->clearAllPartial();
    m_right->clearAllPartial();
}

void DiffView::applyPartialVisuals() {
    m_left->clearAllPartial();
    m_right->clearAllPartial();
    if (m_partialBlockIdx < 0 || m_partialRows.isEmpty()) return;
    const Block& b = m_diffBlocks[m_partialBlockIdx];
    DiffPane* sourcePane = m_partialFromLeftPane ? m_left : m_right;
    DiffPane* otherPane = m_partialFromLeftPane ? m_right : m_left;
    for (int r = b.rowStart; r < b.rowEnd; ++r) {
        const bool sel = m_partialRows.contains(r);
        sourcePane->setRowPartial(r, /*selected=*/sel, /*neutral=*/!sel);
        otherPane->setRowPartial(r, /*selected=*/false, /*neutral=*/true);
    }
}

void DiffView::onLineNumberClicked(bool fromLeftPane, int row, bool shift) {
    const int idx = blockIndexAtRow(row);
    if (idx < 0) {
        clearPartialSelection();
        return;
    }
    if (shift && m_partialBlockIdx == idx && m_partialFromLeftPane == fromLeftPane &&
        m_partialAnchorRow >= 0) {
        const Block& b = m_diffBlocks[idx];
        const int lo = std::min(m_partialAnchorRow, row);
        const int hi = std::max(m_partialAnchorRow, row);
        m_partialRows.clear();
        for (int r = lo; r <= hi; ++r) {
            if (r >= b.rowStart && r < b.rowEnd) m_partialRows.insert(r);
        }
        applyPartialVisuals();
        return;
    }
    const bool sameLoneRow =
        m_partialBlockIdx == idx && m_partialFromLeftPane == fromLeftPane &&
        m_partialRows.size() == 1 && m_partialRows.contains(row);
    if (sameLoneRow) {
        clearPartialSelection();
        return;
    }
    m_partialBlockIdx = idx;
    m_partialFromLeftPane = fromLeftPane;
    m_partialRows.clear();
    m_partialRows.insert(row);
    m_partialAnchorRow = row;
    applyPartialVisuals();
}

void DiffView::onArrowClicked(bool fromLeftPane, int row) {
    const int idx = blockIndexAtRow(row);
    if (idx < 0) return;
    const Block b = m_diffBlocks[idx];

    const bool partialMode = m_partialBlockIdx == idx &&
                             m_partialFromLeftPane == fromLeftPane &&
                             !m_partialRows.isEmpty();

    // Snapshot for arrow-undo before mutating.
    m_arrowUndoStack.push({m_leftLines, m_rightLines});

    // Clear any previous highlight; we'll set the new one based on the destination.
    m_highlightLeftStart = -1;
    m_highlightLeftCount = 0;
    m_highlightRightStart = -1;
    m_highlightRightCount = 0;

    if (partialMode) {
        DiffPane* srcPane = fromLeftPane ? m_left : m_right;
        QList<int> rows = m_partialRows.values();
        std::sort(rows.begin(), rows.end());
        QStringList toCopy;
        const QStringList& srcList = fromLeftPane ? m_leftLines : m_rightLines;
        for (int r : rows) {
            const int sl = srcPane->sourceLineAtRow(r);
            if (sl >= 0 && sl < srcList.size()) toCopy.append(srcList[sl]);
        }
        if (fromLeftPane) {
            for (int i = toCopy.size() - 1; i >= 0; --i)
                m_rightLines.insert(b.rightStart, toCopy[i]);
            m_highlightRightStart = b.rightStart;
            m_highlightRightCount = toCopy.size();
        } else {
            for (int i = toCopy.size() - 1; i >= 0; --i)
                m_leftLines.insert(b.leftStart, toCopy[i]);
            m_highlightLeftStart = b.leftStart;
            m_highlightLeftCount = toCopy.size();
        }
        m_partialBlockIdx = -1;
        m_partialRows.clear();
    } else if (fromLeftPane) {
        QStringList src;
        for (int i = 0; i < b.leftCount; ++i) src.append(m_leftLines[b.leftStart + i]);
        for (int i = 0; i < b.rightCount; ++i) m_rightLines.removeAt(b.rightStart);
        for (int i = src.size() - 1; i >= 0; --i) m_rightLines.insert(b.rightStart, src[i]);
        m_highlightRightStart = b.rightStart;
        m_highlightRightCount = src.size();
    } else {
        QStringList src;
        for (int i = 0; i < b.rightCount; ++i) src.append(m_rightLines[b.rightStart + i]);
        for (int i = 0; i < b.leftCount; ++i) m_leftLines.removeAt(b.leftStart);
        for (int i = src.size() - 1; i >= 0; --i) m_leftLines.insert(b.leftStart, src[i]);
        m_highlightLeftStart = b.leftStart;
        m_highlightLeftCount = src.size();
    }

    // Preserve scroll + cursor position across the rebuild.
    const auto leftCtx = m_left->saveCursor();
    const auto rightCtx = m_right->saveCursor();
    const QWidget* focused = (m_left->hasFocus()) ? static_cast<QWidget*>(m_left)
                                                  : (m_right->hasFocus() ? static_cast<QWidget*>(m_right) : nullptr);
    const int sv = m_left->verticalScrollBar()->value();
    setDirty(true);
    rebuildView();
    m_left->restoreCursor(leftCtx);
    m_right->restoreCursor(rightCtx);
    m_syncing = true;
    m_left->verticalScrollBar()->setValue(sv);
    m_right->verticalScrollBar()->setValue(sv);
    m_syncing = false;
    if (focused == m_left) m_left->setFocus();
    else if (focused == m_right) m_right->setFocus();

    emit currentDifferenceChanged(m_currentDiff, m_diffBlocks.size());
}

void DiffView::setDirty(bool d) {
    if (m_dirty == d) return;
    m_dirty = d;
    emit dirtyChanged(d);
}

void DiffView::undoArrow() {
    if (m_arrowUndoStack.isEmpty()) return;
    const auto snap = m_arrowUndoStack.pop();
    m_leftLines = snap.leftLines;
    m_rightLines = snap.rightLines;
    m_highlightLeftStart = -1;
    m_highlightLeftCount = 0;
    m_highlightRightStart = -1;
    m_highlightRightCount = 0;

    const auto leftCtx = m_left->saveCursor();
    const auto rightCtx = m_right->saveCursor();
    const QWidget* focused = (m_left->hasFocus()) ? static_cast<QWidget*>(m_left)
                                                  : (m_right->hasFocus() ? static_cast<QWidget*>(m_right) : nullptr);
    const int sv = m_left->verticalScrollBar()->value();

    rebuildView();

    m_left->restoreCursor(leftCtx);
    m_right->restoreCursor(rightCtx);
    m_syncing = true;
    m_left->verticalScrollBar()->setValue(sv);
    m_right->verticalScrollBar()->setValue(sv);
    m_syncing = false;
    if (focused == m_left) m_left->setFocus();
    else if (focused == m_right) m_right->setFocus();
    setDirty(true);
}

bool DiffView::save(QString* error) {
    auto writeOne = [&](const QString& path, const QStringList& lines) -> bool {
        if (path.isEmpty()) return true;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = "Could not write " + path;
            return false;
        }
        const QByteArray bytes = lines.join('\n').toUtf8() + '\n';
        if (f.write(bytes) != bytes.size()) {
            if (error) *error = "Short write to " + path;
            return false;
        }
        return true;
    };
    if (!writeOne(m_leftPath, m_leftLines)) return false;
    if (!writeOne(m_rightPath, m_rightLines)) return false;
    setDirty(false);
    m_arrowUndoStack.clear();

    const auto leftCtx = m_left->saveCursor();
    const auto rightCtx = m_right->saveCursor();
    const QWidget* focused = (m_left->hasFocus()) ? static_cast<QWidget*>(m_left)
                                                  : (m_right->hasFocus() ? static_cast<QWidget*>(m_right) : nullptr);
    const int sv = m_left->verticalScrollBar()->value();

    rebuildView();

    m_left->restoreCursor(leftCtx);
    m_right->restoreCursor(rightCtx);
    m_syncing = true;
    m_left->verticalScrollBar()->setValue(sv);
    m_right->verticalScrollBar()->setValue(sv);
    m_syncing = false;
    if (focused == m_left) m_left->setFocus();
    else if (focused == m_right) m_right->setFocus();
    return true;
}
