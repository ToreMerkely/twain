#include "DiffView.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QLocale>
#include <QMainWindow>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>

#include "DebugLog.h"
#include "Diff.h"
#include "DiffOverview.h"
#include "DiffPane.h"
#include "TreeCompareView.h"

namespace {
constexpr int kDefaultDiffPt = 10;
constexpr int kMinDiffPt = 6;
constexpr int kMaxDiffPt = 32;

// Lazily-loaded cache backed by QSettings. -1 sentinel = not yet loaded.
int& diffPtCache() {
    static int v = -1;
    return v;
}

// The bottom current-line widgets render 1pt smaller than the diff panes;
// preserve that delta on zoom.
int bottomPtFor(int diffPt) { return std::max(kMinDiffPt, diffPt - 1); }
}  // namespace

int DiffView::diffFontPt() {
    int& v = diffPtCache();
    if (v < 0) {
        v = std::clamp(QSettings().value("diff/fontPt", kDefaultDiffPt).toInt(),
                       kMinDiffPt, kMaxDiffPt);
    }
    return v;
}

void DiffView::setDiffFontPt(int pt) {
    pt = std::clamp(pt, kMinDiffPt, kMaxDiffPt);
    if (pt == diffFontPt()) return;
    diffPtCache() = pt;
    QSettings().setValue("diff/fontPt", pt);
    const QString msg = QString("Zoom: %1pt").arg(pt);
    for (auto* w : qApp->topLevelWidgets()) {
        for (auto* dv : w->findChildren<DiffView*>()) {
            dv->applyDiffFontSize();
        }
        for (auto* tcv : w->findChildren<TreeCompareView*>()) {
            tcv->applyDiffFontSize();
        }
        if (auto* mw = qobject_cast<QMainWindow*>(w)) {
            mw->statusBar()->showMessage(msg, 1500);
        }
    }
}

void DiffView::adjustDiffFontPt(int delta) {
    setDiffFontPt(diffFontPt() + delta);
}

void DiffView::applyDiffFontSize() {
    const int pt = diffFontPt();
    m_left->setDiffFontPointSize(pt);
    m_right->setDiffFontPointSize(pt);

    QFont bottomFont = m_currentLeftLine->font();
    bottomFont.setPointSize(bottomPtFor(pt));
    const int oneLineHeight = QFontMetrics(bottomFont).height() + 4;
    m_currentLeftLine->setFont(bottomFont);
    m_currentLeftLine->setFixedHeight(oneLineHeight);
    m_currentRightLine->setFont(bottomFont);
    m_currentRightLine->setFixedHeight(oneLineHeight);
}

DiffView::DiffView(QWidget* parent) : QWidget(parent) {
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_left = new DiffPane(m_splitter);
    m_right = new DiffPane(m_splitter);
    m_left->setSide(DiffPane::Side::Left);
    m_right->setSide(DiffPane::Side::Right);

    auto makePaneContainer = [this](DiffPane* pane, QLabel** outLabel) {
        auto* container = new QWidget(m_splitter);
        auto* lay = new QVBoxLayout(container);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        auto* label = new QLabel(container);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setContentsMargins(4, 2, 4, 2);
        label->setTextFormat(Qt::PlainText);
        QFont labelFont = label->font();
        labelFont.setPointSizeF(labelFont.pointSizeF() * 1.3);
        label->setFont(labelFont);
        lay->addWidget(label);
        lay->addWidget(pane, 1);
        *outLabel = label;
        return container;
    };
    QWidget* leftContainer = makePaneContainer(m_left, &m_leftPathLabel);
    QWidget* rightContainer = makePaneContainer(m_right, &m_rightPathLabel);
    m_splitter->addWidget(leftContainer);
    m_splitter->addWidget(rightContainer);
    m_splitter->setSizes({1, 1});

    QFont monoFont("Noto Sans Mono", bottomPtFor(diffFontPt()));
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

    m_overview = new DiffOverview(this);

    auto* mid = new QWidget(this);
    auto* midLayout = new QHBoxLayout(mid);
    midLayout->setContentsMargins(0, 0, 0, 0);
    midLayout->setSpacing(0);
    midLayout->addWidget(m_overview, 0);
    midLayout->addWidget(m_splitter, 1);

    m_searchBar = new QWidget(this);
    {
        auto* lay = new QHBoxLayout(m_searchBar);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(4);
        auto* label = new QLabel("Find:", m_searchBar);
        m_searchEdit = new QLineEdit(m_searchBar);
        m_searchEdit->setClearButtonEnabled(true);
        m_searchStatus = new QLabel(m_searchBar);
        auto* prev = new QToolButton(m_searchBar);
        prev->setText("◀");
        prev->setToolTip("Previous (Shift+Enter / Shift+F3)");
        auto* next = new QToolButton(m_searchBar);
        next->setText("▶");
        next->setToolTip("Next (Enter / F3)");
        auto* close = new QToolButton(m_searchBar);
        close->setText("✕");
        close->setToolTip("Close (Esc)");
        lay->addWidget(label);
        lay->addWidget(m_searchEdit, 1);
        lay->addWidget(m_searchStatus);
        lay->addWidget(prev);
        lay->addWidget(next);
        lay->addWidget(close);

        connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
            m_left->setSearchTerm(t);
            m_right->setSearchTerm(t);
            updateSearchStatus();
        });
        connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
            DiffPane* p = currentSearchTarget();
            p->findNext();
            updateSearchStatus();
        });
        connect(next, &QToolButton::clicked, this, [this]() {
            currentSearchTarget()->findNext();
            updateSearchStatus();
        });
        connect(prev, &QToolButton::clicked, this, [this]() {
            currentSearchTarget()->findPrev();
            updateSearchStatus();
        });
        connect(close, &QToolButton::clicked, this, [this]() {
            m_searchEdit->clear();
            m_searchBar->hide();
            m_left->setSearchTerm({});
            m_right->setSearchTerm({});
        });
        m_searchEdit->installEventFilter(this);
    }
    m_searchBar->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_searchBar, 0);
    layout->addWidget(mid, 1);
    layout->addWidget(bottom, 0);

    connect(m_overview, &DiffOverview::scrollRequested, this, [this](int top) {
        m_syncing = true;
        m_left->verticalScrollBar()->setValue(top);
        m_right->verticalScrollBar()->setValue(top);
        m_syncing = false;
        // Re-emit so overview viewport box updates immediately.
        const int lineH = m_left->fontMetrics().lineSpacing();
        const int visibleLines = lineH > 0 ? m_left->viewport()->height() / lineH : 0;
        m_overview->setViewport(m_left->verticalScrollBar()->value(), visibleLines);
    });

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        // Many editors atomically replace the file (write tmp + rename); the
        // watcher loses the inode, so re-add the path if it still exists.
        m_watcher->removePath(path);
        if (QFile::exists(path)) m_watcher->addPath(path);
        if (m_ignoreNextWatch) return;
        if (m_dirty) return;  // don't clobber in-progress edits
        // Reload, preserving cursor/scroll where possible.
        const auto leftCtx = m_left->saveCursor();
        const auto rightCtx = m_right->saveCursor();
        const int sv = m_left->verticalScrollBar()->value();
        QString err;
        if (!setFiles(m_leftPath, m_rightPath, &err)) return;
        m_left->restoreCursor(leftCtx);
        m_right->restoreCursor(rightCtx);
        m_syncing = true;
        m_left->verticalScrollBar()->setValue(sv);
        m_right->verticalScrollBar()->setValue(sv);
        m_syncing = false;
    });

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
    connect(m_left, &DiffPane::truncationMarkerClicked, this, &DiffView::loadMore);
    connect(m_right, &DiffPane::truncationMarkerClicked, this, &DiffView::loadMore);
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
    if (m_overview) {
        const int lineH = source->fontMetrics().lineSpacing();
        const int visibleLines = lineH > 0 ? source->viewport()->height() / lineH : 0;
        m_overview->setViewport(value, visibleLines);
    }
}

static bool looksBinary(const QByteArray& bytes) {
    const int probe = qMin<int>(bytes.size(), 8192);
    for (int i = 0; i < probe; ++i) {
        if (bytes[i] == '\0') return true;
    }
    return false;
}

using FileLoadInfo = DiffView::FileLoadInfo;

static constexpr qint64 kLazyLoadThresholdBytes = 10 * 1024 * 1024;  // 10 MB
static constexpr int kLazyLoadMaxLines = 10000;

static QStringList streamLines(QFile& f, int maxLines) {
    QStringList lines;
    lines.reserve(maxLines);
    for (int i = 0; i < maxLines; ++i) {
        if (f.atEnd()) break;
        QByteArray raw = f.readLine();
        while (!raw.isEmpty() && (raw.endsWith('\n') || raw.endsWith('\r'))) {
            raw.chop(1);
        }
        lines.append(QString::fromUtf8(raw));
    }
    return lines;
}

static QStringList readFileLinesStreaming(QFile& f, FileLoadInfo* info, bool* ok, QString* err,
                                          const QString& path) {
    TWAIN_SCOPED_VAR(_t, "  readFileLines.streaming");
    // Probe the head for binary content before committing to the read.
    const QByteArray probe = f.peek(8192);
    if (looksBinary(probe)) {
        if (ok) *ok = false;
        if (err) *err = path + " appears to be a binary file";
        return {};
    }
    QStringList lines = streamLines(f, kLazyLoadMaxLines);
    info->streamOffset = f.pos();
    info->truncated = !f.atEnd();
    if (ok) *ok = true;
    TWAIN_SCOPED_NOTE(_t, QString("lines=%1 truncated=%2")
                             .arg(lines.size()).arg(info->truncated ? "yes" : "no"));
    return lines;
}

// Load up to kLazyLoadMaxLines more lines. For the byte path the rest of the
// file is already in info->pendingLines; for the streaming path we reopen the
// file and resume from info->streamOffset.
static QStringList loadMoreLines(const QString& path, FileLoadInfo* info) {
    TWAIN_SCOPED_VAR(_t, "loadMoreLines");
    if (!info || !info->truncated) return {};
    QStringList more;
    if (!info->pendingLines.isEmpty()) {
        const int n = qMin(info->pendingLines.size(), kLazyLoadMaxLines);
        more = info->pendingLines.mid(0, n);
        info->pendingLines.erase(info->pendingLines.begin(),
                                 info->pendingLines.begin() + n);
        info->truncated = !info->pendingLines.isEmpty();
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        if (!f.seek(info->streamOffset)) return {};
        more = streamLines(f, kLazyLoadMaxLines);
        info->streamOffset = f.pos();
        info->truncated = !f.atEnd();
    }
    TWAIN_SCOPED_NOTE(_t, QString("path=%1 more=%2 truncated=%3")
                             .arg(path).arg(more.size())
                             .arg(info->truncated ? "yes" : "no"));
    return more;
}

static QStringList readFileLines(const QString& path, FileLoadInfo* info,
                                 bool* ok, QString* err) {
    TWAIN_SCOPED_VAR(_t, "readFileLines");
    if (info) *info = {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        if (err) *err = "Could not open " + path;
        return {};
    }
    const qint64 totalBytes = QFileInfo(path).size();
    if (info) info->totalBytes = totalBytes;

    if (totalBytes > kLazyLoadThresholdBytes) {
        QStringList lines = readFileLinesStreaming(f, info, ok, err, path);
        TWAIN_SCOPED_NOTE(_t, QString("path=%1 bytes=%2 lines=%3 truncated=%4")
                                 .arg(path).arg(totalBytes).arg(lines.size())
                                 .arg(info && info->truncated ? "yes" : "no"));
        return lines;
    }

    QByteArray bytes;
    {
        TWAIN_SCOPED("  readFileLines.readAll");
        bytes = f.readAll();
    }
    if (looksBinary(bytes)) {
        if (ok) *ok = false;
        if (err) *err = path + " appears to be a binary file";
        return {};
    }
    QString text;
    {
        TWAIN_SCOPED("  readFileLines.decodeUtf8");
        text = QString::fromUtf8(bytes);
    }
    if (text.contains('\r')) {
        TWAIN_SCOPED("  readFileLines.normalizeLineEndings");
        text.replace("\r\n", "\n");
        text.replace('\r', '\n');
    }
    QStringList lines;
    {
        TWAIN_SCOPED("  readFileLines.split");
        lines = text.split('\n');
    }
    // split() leaves a trailing empty element when text ends with '\n' — drop it
    if (!lines.isEmpty() && lines.last().isEmpty() && text.endsWith('\n')) {
        lines.removeLast();
    }
    if (info) info->totalLines = lines.size();
    if (lines.size() > kLazyLoadMaxLines) {
        if (info) {
            info->pendingLines = lines.mid(kLazyLoadMaxLines);
            info->truncated = true;
        }
        lines.resize(kLazyLoadMaxLines);
    }
    TWAIN_SCOPED_NOTE(_t, QString("path=%1 bytes=%2 lines=%3 truncated=%4")
                             .arg(path).arg(bytes.size()).arg(lines.size())
                             .arg(info && info->truncated ? "yes" : "no"));
    if (ok) *ok = true;
    return lines;
}

static void filterBlanks(const QStringList& orig, QStringList* out, QVector<int>* origIdx) {
    TWAIN_SCOPED_VAR(_t, "filterBlanks");
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
    TWAIN_SCOPED_NOTE(_t, QString("in=%1 out=%2").arg(orig.size()).arg(out->size()));
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
    TWAIN_SCOPED_VAR(_tBuild, "buildAlignedRows");
    AlignedBuildResult res;
    leftRows.clear();
    rightRows.clear();
#ifdef TWAIN_DEBUG
    int lineDiffCalls = 0;
    int alignBlockCalls = 0;
    double lineDiffTotalMs = 0.0;
    double alignBlockTotalMs = 0.0;
#endif

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
#ifdef TWAIN_DEBUG
            const auto _abStart = std::chrono::steady_clock::now();
#endif
            pairs = Diff::alignBlock(leftBlock, rightBlock);
#ifdef TWAIN_DEBUG
            const auto _abEnd = std::chrono::steady_clock::now();
            alignBlockCalls++;
            alignBlockTotalMs += std::chrono::duration<double, std::milli>(_abEnd - _abStart).count();
#endif
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
#ifdef TWAIN_DEBUG
                const auto _ldStart = std::chrono::steady_clock::now();
#endif
                const auto ld = Diff::lineDiff(lr.text, rr.text, diffOpts);
#ifdef TWAIN_DEBUG
                const auto _ldEnd = std::chrono::steady_clock::now();
                lineDiffCalls++;
                lineDiffTotalMs += std::chrono::duration<double, std::milli>(_ldEnd - _ldStart).count();
#endif
                lr.segments = ld.left;
                rr.segments = ld.right;
            }
            leftRows.append(lr);
            rightRows.append(rr);
        }
        block.rowEnd = leftRows.size();
        res.blocks.append(block);
    }
#ifdef TWAIN_DEBUG
    TWAIN_SCOPED_NOTE(_tBuild,
        QString("blocks=%1 rows=%2 lineDiff(n=%3,total=%4ms) alignBlock(n=%5,total=%6ms)")
            .arg(res.blocks.size())
            .arg(leftRows.size())
            .arg(lineDiffCalls)
            .arg(lineDiffTotalMs, 0, 'f', 2)
            .arg(alignBlockCalls)
            .arg(alignBlockTotalMs, 0, 'f', 2));
#endif
    return res;
}

bool DiffView::setFiles(const QString& leftPath, const QString& rightPath, QString* error) {
    TWAIN_LOG(QString("setFiles begin left=%1 right=%2").arg(leftPath, rightPath));
    TWAIN_SCOPED("DiffView::setFiles");
    QStringList leftLines, rightLines;
    FileLoadInfo leftInfo, rightInfo;
    if (!leftPath.isEmpty()) {
        bool okL = false;
        QString errL;
        leftLines = readFileLines(leftPath, &leftInfo, &okL, &errL);
        if (!okL) {
            if (error) *error = errL;
            return false;
        }
    }
    if (!rightPath.isEmpty()) {
        bool okR = false;
        QString errR;
        rightLines = readFileLines(rightPath, &rightInfo, &okR, &errR);
        if (!okR) {
            if (error) *error = errR;
            return false;
        }
    }
    m_leftLoadInfo = leftInfo;
    m_rightLoadInfo = rightInfo;

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

    if (m_watcher && !m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    m_leftPath = leftPath;
    m_rightPath = rightPath;
    m_leftLines = leftLines;
    m_rightLines = rightLines;
    if (m_watcher) {
        if (!leftPath.isEmpty() && QFile::exists(leftPath)) m_watcher->addPath(leftPath);
        if (!rightPath.isEmpty() && QFile::exists(rightPath)) m_watcher->addPath(rightPath);
    }
    auto formatLabel = [](const QString& path, const FileLoadInfo& info) -> QString {
        if (!info.truncated) return path;
        const QString sizeStr = QLocale().formattedDataSize(info.totalBytes, 1);
        const QString capStr = QLocale().toString(kLazyLoadMaxLines);
        QString prefix;
        if (info.totalLines > 0) {
            prefix = QString("[TRUNCATED to %1 of %2 lines, %3]  ")
                         .arg(capStr, QLocale().toString(info.totalLines), sizeStr);
        } else {
            prefix = QString("[TRUNCATED to %1 lines, %2]  ").arg(capStr, sizeStr);
        }
        return prefix + path;
    };
    auto formatTooltip = [](const QString& path, const FileLoadInfo& info) -> QString {
        if (!info.truncated) return path;
        const QString sizeStr = QLocale().formattedDataSize(info.totalBytes, 1);
        const QString capStr = QLocale().toString(kLazyLoadMaxLines);
        if (info.totalLines > 0) {
            return QString("%1\n\nShowing first %2 of %3 lines (%4)")
                .arg(path, capStr, QLocale().toString(info.totalLines), sizeStr);
        }
        return QString("%1\n\nShowing first %2 lines (%3)").arg(path, capStr, sizeStr);
    };
    m_leftPathLabel->setText(formatLabel(leftPath, m_leftLoadInfo));
    m_leftPathLabel->setToolTip(formatTooltip(leftPath, m_leftLoadInfo));
    m_rightPathLabel->setText(formatLabel(rightPath, m_rightLoadInfo));
    m_rightPathLabel->setToolTip(formatTooltip(rightPath, m_rightLoadInfo));

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
    TWAIN_SCOPED_VAR(_tRebuild, "DiffView::rebuildView");
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

    QVector<Diff::Hunk> hunks;
    {
        TWAIN_SCOPED_VAR(_tCompute, "Diff::compute");
        hunks = Diff::compute(compareLeft, compareRight, diffOpts);
        TWAIN_SCOPED_NOTE(_tCompute, QString("nL=%1 nR=%2 hunks=%3")
                                        .arg(compareLeft.size())
                                        .arg(compareRight.size())
                                        .arg(hunks.size()));
    }
    QVector<DiffRow> leftRows, rightRows;
    HighlightRange hl;
    hl.leftStart = m_highlightLeftStart;
    hl.leftCount = m_highlightLeftCount;
    hl.rightStart = m_highlightRightStart;
    hl.rightCount = m_highlightRightCount;
    auto res = buildAlignedRows(m_leftLines, m_rightLines, leftMap, rightMap,
                                hunks, diffOpts, hl, leftRows, rightRows);
    m_diffBlocks = res.blocks;

    if (m_leftLoadInfo.truncated || m_rightLoadInfo.truncated) {
        auto markerText = [](const FileLoadInfo& info, int loaded) -> QString {
            if (!info.truncated) return QString();
            const QString capStr = QLocale().toString(kLazyLoadMaxLines);
            QString tail;
            if (info.totalLines > 0) {
                tail = QString("%1 of %2 lines shown")
                           .arg(QLocale().toString(loaded),
                                QLocale().toString(info.totalLines));
            } else {
                tail = QString("%1 lines shown, %2 total on disk")
                           .arg(QLocale().toString(loaded),
                                QLocale().formattedDataSize(info.totalBytes, 1));
            }
            return QString("► Click to load next %1 lines  (%2) ◄").arg(capStr, tail);
        };
        // The bar spans three rows for visibility: blank, text, blank.
        // All rows are flagged as markers so they share the yellow styling
        // and any of them is clickable; loadMore() only loads on sides where
        // info.truncated is still true.
        const QString leftText = markerText(m_leftLoadInfo, m_leftLines.size());
        const QString rightText = markerText(m_rightLoadInfo, m_rightLines.size());
        auto pushMarker = [&](const QString& l, const QString& r) {
            DiffRow lm{-1, l, Diff::Op::Equal, true, {}, false, false, false, true};
            DiffRow rm{-1, r, Diff::Op::Equal, true, {}, false, false, false, true};
            leftRows.append(lm);
            rightRows.append(rm);
        };
        pushMarker({}, {});
        pushMarker(leftText, rightText);
        pushMarker({}, {});
    }

    {
        TWAIN_SCOPED("DiffPane::setRows left");
        m_left->setRows(leftRows);
    }
    {
        TWAIN_SCOPED("DiffPane::setRows right");
        m_right->setRows(rightRows);
    }

    if (m_overview) {
        QVector<DiffOverview::Mark> marks;
        marks.reserve(m_diffBlocks.size());
        for (const auto& b : m_diffBlocks) marks.append({b.rowStart, b.rowEnd});
        m_overview->setTotalRows(leftRows.size());
        m_overview->setMarks(marks);
        const int lineH = m_left->fontMetrics().lineSpacing();
        const int visibleLines = lineH > 0 ? m_left->viewport()->height() / lineH : 0;
        m_overview->setViewport(m_left->verticalScrollBar()->value(), visibleLines);
    }
}

void DiffView::loadMore() {
    TWAIN_SCOPED("DiffView::loadMore");
    bool changed = false;
    if (m_leftLoadInfo.truncated) {
        const QStringList more = ::loadMoreLines(m_leftPath, &m_leftLoadInfo);
        if (!more.isEmpty()) {
            m_leftLines.append(more);
            changed = true;
        }
    }
    if (m_rightLoadInfo.truncated) {
        const QStringList more = ::loadMoreLines(m_rightPath, &m_rightLoadInfo);
        if (!more.isEmpty()) {
            m_rightLines.append(more);
            changed = true;
        }
    }
    if (!changed) return;

    // Preserve scroll position across the rebuild — without this, setPlainText
    // inside DiffPane::setRows resets the scrollbar to 0.
    const int leftScroll = m_left->verticalScrollBar()->value();
    const int rightScroll = m_right->verticalScrollBar()->value();

    auto formatLabel = [](const QString& path, const FileLoadInfo& info) -> QString {
        if (!info.truncated) return path;
        const QString sizeStr = QLocale().formattedDataSize(info.totalBytes, 1);
        const QString capStr = QLocale().toString(kLazyLoadMaxLines);
        QString prefix;
        if (info.totalLines > 0) {
            prefix = QString("[TRUNCATED to %1 of %2 lines, %3]  ")
                         .arg(capStr, QLocale().toString(info.totalLines), sizeStr);
        } else {
            prefix = QString("[TRUNCATED to %1 lines, %2]  ").arg(capStr, sizeStr);
        }
        return prefix + path;
    };
    m_leftPathLabel->setText(formatLabel(m_leftPath, m_leftLoadInfo));
    m_rightPathLabel->setText(formatLabel(m_rightPath, m_rightLoadInfo));

    rebuildView();

    m_left->verticalScrollBar()->setValue(leftScroll);
    m_right->verticalScrollBar()->setValue(rightScroll);

    // diff count or truncation status may have changed; let the chrome update.
    emit currentDifferenceChanged(m_currentDiff, m_diffBlocks.size());
}

void DiffView::setOptions(Options opts) {
    m_options = opts;
    if (!m_leftPath.isEmpty() || !m_rightPath.isEmpty()) {
        setFiles(m_leftPath, m_rightPath);
    }
}

void DiffView::nextDifference() {
    const bool truncated = m_leftLoadInfo.truncated || m_rightLoadInfo.truncated;
    const bool onMarker = m_left->isCursorOnTruncationMarker() ||
                          m_right->isCursorOnTruncationMarker();

    if (onMarker && truncated) {
        // Markers always sit at the bottom, so this row is where the newly
        // loaded content will begin.
        const int markerRow = m_left->textCursor().blockNumber();
        loadMore();
        const bool stillTruncated =
            m_leftLoadInfo.truncated || m_rightLoadInfo.truncated;
        if (stillTruncated) {
            scrollToBottom();
            return;
        }
        // File is now fully loaded: jump to the first diff in the newly
        // revealed region (which is what Ctrl+N was meant to reach).
        for (int i = 0; i < m_diffBlocks.size(); ++i) {
            if (m_diffBlocks[i].rowStart >= markerRow) {
                goToDiff(i);
                return;
            }
        }
        scrollToBottom();
        return;
    }

    // Find the first diff strictly past the cursor row. Using cursor row
    // (rather than m_currentDiff) keeps navigation in sync after manual
    // scrolling, clicking, or a loadMore that moved the cursor.
    const int cursorRow = m_left->textCursor().blockNumber();
    for (int i = 0; i < m_diffBlocks.size(); ++i) {
        if (m_diffBlocks[i].rowStart > cursorRow) {
            goToDiff(i);
            return;
        }
    }

    if (truncated) scrollToBottom();
}

void DiffView::scrollToBottom() {
    const int lastRow = m_left->document()->blockCount() - 1;
    if (lastRow <= 0) return;
    QTextCursor c(m_left->document()->findBlockByNumber(lastRow));
    m_left->setTextCursor(c);
    QScrollBar* vb = m_left->verticalScrollBar();
    vb->setValue(vb->maximum());
    m_left->setFocus();
    m_syncing = true;
    m_right->verticalScrollBar()->setValue(vb->value());
    m_syncing = false;
}

void DiffView::prevDifference() {
    if (m_diffBlocks.isEmpty()) return;
    const int cursorRow = m_left->textCursor().blockNumber();
    for (int i = m_diffBlocks.size() - 1; i >= 0; --i) {
        if (m_diffBlocks[i].rowStart < cursorRow) {
            goToDiff(i);
            return;
        }
    }
}

void DiffView::goToDiff(int index) {
    if (index < 0 || index >= m_diffBlocks.size()) return;
    m_currentDiff = index;
    const int row = m_diffBlocks[index].rowStart;

    QTextCursor cursor(m_left->document()->findBlockByNumber(row));
    m_left->setTextCursor(cursor);
    const int lineH = m_left->fontMetrics().lineSpacing();
    const int visibleLines = lineH > 0 ? m_left->viewport()->height() / lineH : 0;
    const int topLine = std::max(0, row - visibleLines / 4);
    m_left->verticalScrollBar()->setValue(topLine);
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
            sel.format.setForeground(QColor(200, 0, 0));
            sel.format.setFontWeight(QFont::Bold);
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
    m_ignoreNextWatch = true;
    QTimer::singleShot(500, this, [this]() { m_ignoreNextWatch = false; });
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

void DiffView::showSearchBar() {
    m_searchBar->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

DiffPane* DiffView::currentSearchTarget() const {
    return m_right->hasFocus() ? m_right : m_left;
}

void DiffView::updateSearchStatus() {
    const QString t = m_searchEdit->text();
    if (t.isEmpty()) {
        m_searchStatus->clear();
        return;
    }
    const int lc = m_left->matchCount();
    const int rc = m_right->matchCount();
    const DiffPane* target = currentSearchTarget();
    const int cur = target->currentMatchIndex();
    const int total = target->matchCount();
    if (total == 0 && lc == 0 && rc == 0) {
        m_searchStatus->setText("no matches");
    } else {
        m_searchStatus->setText(QString("%1/%2  (L:%3 R:%4)")
                                    .arg(cur >= 0 ? cur + 1 : 0)
                                    .arg(total)
                                    .arg(lc)
                                    .arg(rc));
    }
}

bool DiffView::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            m_searchEdit->clear();
            m_searchBar->hide();
            m_left->setSearchTerm({});
            m_right->setSearchTerm({});
            (m_left->hasFocus() ? m_left : m_right)->setFocus();
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            (ke->modifiers() & Qt::ShiftModifier)) {
            currentSearchTarget()->findPrev();
            updateSearchStatus();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
