#include "DiffView.h"

#include <QFile>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>

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

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);

    connect(m_left->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int v) { syncScroll(m_left, m_right, v); });
    connect(m_right->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int v) { syncScroll(m_right, m_left, v); });
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

static void buildAlignedRows(const QStringList& left, const QStringList& right,
                             const QVector<int>& leftMap, const QVector<int>& rightMap,
                             const QVector<Diff::Hunk>& hunks, Diff::Options diffOpts,
                             QVector<DiffRow>& leftRows, QVector<DiffRow>& rightRows,
                             QVector<int>& diffRowStarts) {
    leftRows.clear();
    rightRows.clear();
    diffRowStarts.clear();

    int i = 0;
    while (i < hunks.size()) {
        const auto& h = hunks[i];
        if (h.op == Diff::Op::Equal) {
            for (int k = 0; k < h.leftCount; ++k) {
                const int li = leftMap[h.leftStart + k];
                const int ri = rightMap[h.rightStart + k];
                leftRows.append({li, left[li], Diff::Op::Equal, false, {}});
                rightRows.append({ri, right[ri], Diff::Op::Equal, false, {}});
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

        diffRowStarts.append(leftRows.size());
        const int rows = qMax(hasDel ? del.leftCount : 0, hasIns ? ins.rightCount : 0);
        for (int k = 0; k < rows; ++k) {
            const bool leftReal = hasDel && k < del.leftCount;
            const bool rightReal = hasIns && k < ins.rightCount;

            DiffRow lr, rr;
            if (leftReal) {
                const int li = leftMap[del.leftStart + k];
                lr = {li, left[li], Diff::Op::Delete, false, {}};
            } else {
                lr = {-1, QString(), Diff::Op::Delete, true, {}};
            }
            if (rightReal) {
                const int ri = rightMap[ins.rightStart + k];
                rr = {ri, right[ri], Diff::Op::Insert, false, {}};
            } else {
                rr = {-1, QString(), Diff::Op::Insert, true, {}};
            }
            if (leftReal && rightReal) {
                const auto ld = Diff::lineDiff(lr.text, rr.text, diffOpts);
                lr.segments = ld.left;
                rr.segments = ld.right;
            }
            leftRows.append(lr);
            rightRows.append(rr);
        }
    }
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

    const auto hunks = Diff::compute(compareLeft, compareRight, diffOpts);
    QVector<DiffRow> leftRows, rightRows;
    buildAlignedRows(leftLines, rightLines, leftMap, rightMap, hunks, diffOpts,
                     leftRows, rightRows, m_diffRows);

    m_leftPath = leftPath;
    m_rightPath = rightPath;
    m_left->setLanguageFromPath(leftPath.isEmpty() ? rightPath : leftPath);
    m_right->setLanguageFromPath(rightPath.isEmpty() ? leftPath : rightPath);
    m_left->setRows(leftRows);
    m_right->setRows(rightRows);

    m_currentDiff = -1;
    m_left->verticalScrollBar()->setValue(0);
    m_right->verticalScrollBar()->setValue(0);
    emit currentDifferenceChanged(-1, m_diffRows.size());
    return true;
}

void DiffView::setOptions(Options opts) {
    m_options = opts;
    if (!m_leftPath.isEmpty() || !m_rightPath.isEmpty()) {
        setFiles(m_leftPath, m_rightPath);
    }
}

void DiffView::nextDifference() {
    if (m_diffRows.isEmpty()) return;
    if (m_currentDiff + 1 >= m_diffRows.size()) return;
    goToDiff(m_currentDiff + 1);
}

void DiffView::prevDifference() {
    if (m_diffRows.isEmpty()) return;
    if (m_currentDiff <= 0) return;
    goToDiff(m_currentDiff - 1);
}

void DiffView::goToDiff(int index) {
    if (index < 0 || index >= m_diffRows.size()) return;
    m_currentDiff = index;
    const int row = m_diffRows[index];

    QTextCursor cursor(m_left->document()->findBlockByNumber(row));
    m_left->setTextCursor(cursor);
    m_left->centerCursor();
    // Sync right pane explicitly — centerCursor doesn't always fire
    // valueChanged on the scrollbar, and setting right's text cursor
    // separately can land a row off.
    m_syncing = true;
    m_right->verticalScrollBar()->setValue(m_left->verticalScrollBar()->value());
    m_syncing = false;

    emit currentDifferenceChanged(m_currentDiff, m_diffRows.size());
}
