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

static void buildAlignedRows(const QStringList& left, const QStringList& right,
                             const QVector<Diff::Hunk>& hunks,
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
                leftRows.append({h.leftStart + k, left[h.leftStart + k], Diff::Op::Equal, false});
                rightRows.append({h.rightStart + k, right[h.rightStart + k], Diff::Op::Equal, false});
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
            if (hasDel && k < del.leftCount) {
                leftRows.append({del.leftStart + k, left[del.leftStart + k], Diff::Op::Delete, false});
            } else {
                leftRows.append({-1, QString(), Diff::Op::Delete, true});
            }
            if (hasIns && k < ins.rightCount) {
                rightRows.append({ins.rightStart + k, right[ins.rightStart + k], Diff::Op::Insert, false});
            } else {
                rightRows.append({-1, QString(), Diff::Op::Insert, true});
            }
        }
    }
}

bool DiffView::setFiles(const QString& leftPath, const QString& rightPath, QString* error) {
    bool okL = false, okR = false;
    QString errL, errR;
    QStringList leftLines = readFileLines(leftPath, &okL, &errL);
    QStringList rightLines = readFileLines(rightPath, &okR, &errR);
    if (!okL) {
        if (error) *error = errL;
        return false;
    }
    if (!okR) {
        if (error) *error = errR;
        return false;
    }

    const auto hunks = Diff::compute(leftLines, rightLines);
    QVector<DiffRow> leftRows, rightRows;
    buildAlignedRows(leftLines, rightLines, hunks, leftRows, rightRows, m_diffRows);

    m_left->setLanguageFromPath(leftPath);
    m_right->setLanguageFromPath(rightPath);
    m_left->setRows(leftRows);
    m_right->setRows(rightRows);
    m_currentDiff = -1;
    if (!m_diffRows.isEmpty()) {
        goToDiff(0);
    } else {
        emit currentDifferenceChanged(-1, 0);
    }
    return true;
}

void DiffView::nextDifference() {
    if (m_diffRows.isEmpty()) return;
    int next = (m_currentDiff + 1) % m_diffRows.size();
    goToDiff(next);
}

void DiffView::prevDifference() {
    if (m_diffRows.isEmpty()) return;
    int prev = m_currentDiff <= 0 ? m_diffRows.size() - 1 : m_currentDiff - 1;
    goToDiff(prev);
}

void DiffView::goToDiff(int index) {
    if (index < 0 || index >= m_diffRows.size()) return;
    m_currentDiff = index;
    const int row = m_diffRows[index];

    QTextCursor cursor(m_left->document()->findBlockByNumber(row));
    m_left->setTextCursor(cursor);
    m_left->centerCursor();
    // right pane follows via scroll sync, but ensure cursor matches too
    QTextCursor cursorR(m_right->document()->findBlockByNumber(row));
    m_right->setTextCursor(cursorR);

    emit currentDifferenceChanged(m_currentDiff, m_diffRows.size());
}
