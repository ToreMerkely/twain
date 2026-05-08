#include "DiffPane.h"

#include <QColor>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QWheelEvent>
#include <QTextCharFormat>
#include <QTextCursor>

namespace {

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(DiffPane* pane) : QWidget(pane), m_pane(pane) {}

    QSize sizeHint() const override { return QSize(m_pane->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { m_pane->lineNumberAreaPaintEvent(event); }
    void mousePressEvent(QMouseEvent* event) override { m_pane->lineNumberAreaMousePressEvent(event); }

private:
    DiffPane* m_pane;
};

QColor colorFor(Diff::Op op, bool filler) {
    if (filler) return QColor(230, 230, 230);
    switch (op) {
        case Diff::Op::Delete: return QColor(255, 215, 215);
        case Diff::Op::Insert: return QColor(255, 215, 215);
        case Diff::Op::Equal:
        default: return QColor(Qt::transparent);
    }
}

QBrush fillerBrush() {
    return QBrush(QColor(160, 160, 160), Qt::BDiagPattern);
}

QColor segmentColor() { return QColor(200, 0, 0); }
QColor arrowColor() { return QColor(255, 195, 50); }
QColor arrowEdgeColor() { return QColor(180, 120, 0); }
QColor bracketColor() { return QColor(60, 60, 60); }
QColor partialBgColor() { return QColor(200, 220, 255); }
QColor partialArrowColor() { return QColor(60, 130, 255); }
QColor partialArrowEdgeColor() { return QColor(20, 70, 160); }
constexpr int kArrowWidth = 12;
constexpr int kArrowHeight = 12;
constexpr int kArrowPad = 6;
constexpr int kBracketPenWidth = 1;
constexpr int kArrowNotch = 3;

}  // namespace

DiffPane::DiffPane(QWidget* parent) : QPlainTextEdit(parent) {
    QFont font("JetBrains Mono", 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setReadOnly(true);

    m_lineNumberArea = new LineNumberArea(this);

    setReadOnly(false);
    setUndoRedoEnabled(true);
    setCursorWidth(2);

    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_loading) emit contentEdited();
    });

    connect(this, &QPlainTextEdit::blockCountChanged, this, &DiffPane::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &DiffPane::updateLineNumberArea);

    updateLineNumberAreaWidth();
}

void DiffPane::setSide(Side s) {
    m_side = s;
    updateLineNumberAreaWidth();
    m_lineNumberArea->update();
}

void DiffPane::setRows(const QVector<DiffRow>& rows) {
    m_loading = true;
    m_rows = rows;
    int maxSrc = 0;
    QStringList texts;
    texts.reserve(rows.size());
    for (const auto& r : rows) {
        texts << r.text;
        if (r.sourceLine + 1 > maxSrc) maxSrc = r.sourceLine + 1;
    }
    m_maxSourceLine = maxSrc;
    setPlainText(texts.join('\n'));
    applyRowBackgrounds();
    m_lineNumberArea->update();
    m_loading = false;
}

QStringList DiffPane::extractContent() const {
    QStringList out;
    QTextBlock block = document()->firstBlock();
    int row = 0;
    while (block.isValid()) {
        const bool wasFiller = (row < m_rows.size()) && m_rows[row].filler;
        const QString text = block.text();
        // Pristine empty filler rows aren't part of the file — skip them.
        if (!(wasFiller && text.isEmpty())) {
            out.append(text);
        }
        block = block.next();
        ++row;
    }
    return out;
}

DiffPane::CursorContext DiffPane::saveCursor() const {
    CursorContext ctx;
    QTextCursor c = textCursor();
    const int targetBlock = c.blockNumber();
    ctx.column = c.positionInBlock();

    int fileLine = 0;
    QTextBlock block = document()->firstBlock();
    int row = 0;
    while (block.isValid() && row < targetBlock) {
        const bool wasFiller = (row < m_rows.size()) && m_rows[row].filler;
        const QString text = block.text();
        if (!(wasFiller && text.isEmpty())) ++fileLine;
        block = block.next();
        ++row;
    }
    // If the cursor's own block is a pristine filler, snap to the next real line.
    const bool curWasFiller =
        (targetBlock < m_rows.size()) && m_rows[targetBlock].filler;
    const QString curText = block.isValid() ? block.text() : QString();
    if (curWasFiller && curText.isEmpty()) {
        // fileLine already points at the next-real-line after this filler.
        ctx.column = 0;
    }
    ctx.fileLine = fileLine;
    return ctx;
}

void DiffPane::restoreCursor(CursorContext ctx) {
    if (ctx.fileLine < 0) return;
    int rowToFocus = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].sourceLine == ctx.fileLine && !m_rows[i].filler) {
            rowToFocus = i;
            break;
        }
    }
    if (rowToFocus < 0) return;
    QTextBlock blk = document()->findBlockByNumber(rowToFocus);
    if (!blk.isValid()) return;
    QTextCursor c(blk);
    const int col = qMin(ctx.column, blk.length() - 1);
    c.setPosition(blk.position() + col);
    setTextCursor(c);
}

void DiffPane::applyRowBackgrounds() {
    QList<QTextEdit::ExtraSelection> selections;
    selections.reserve(m_rows.size());

    QTextDocument* doc = document();
    QTextBlock block = doc->firstBlock();
    for (int i = 0; i < m_rows.size(); ++i, block = block.next()) {
        const auto& r = m_rows[i];
        if (!block.isValid()) continue;

        if (r.partialSelected) {
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(partialBgColor());
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block);
            sel.cursor.clearSelection();
            selections.append(sel);
        } else if (r.partialNeutral) {
            // Suppress the normal red/gray background.
        } else if (r.kind != Diff::Op::Equal || r.filler) {
            QTextEdit::ExtraSelection sel;
            if (r.filler) {
                sel.format.setBackground(fillerBrush());
            } else {
                sel.format.setBackground(colorFor(r.kind, r.filler));
            }
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block);
            sel.cursor.clearSelection();
            selections.append(sel);
        }

        if (r.partialSelected || r.partialNeutral) continue;
        for (const auto& seg : r.segments) {
            if (!seg.differ) continue;
            QTextEdit::ExtraSelection ssel;
            ssel.format.setForeground(segmentColor());
            ssel.format.setFontWeight(QFont::Bold);
            QTextCursor c(block);
            const int start = block.position() + seg.start;
            const int end = start + seg.length;
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            ssel.cursor = c;
            selections.append(ssel);
        }
    }
    setExtraSelections(selections);
}

int DiffPane::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(qMax(1, blockCount()), m_maxSourceLine);
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    digits = qMax(digits, 3);
    return 8 + fontMetrics().horizontalAdvance('9') * digits + kArrowWidth + kArrowPad;
}

void DiffPane::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void DiffPane::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void DiffPane::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void DiffPane::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        bool hadPartial = false;
        for (const auto& r : m_rows) {
            if (r.partialSelected) { hadPartial = true; break; }
        }
        emit clearPartialRequested();
        if (hadPartial) {
            event->accept();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}

void DiffPane::mousePressEvent(QMouseEvent* event) {
    emit clearPartialRequested();
    QPlainTextEdit::mousePressEvent(event);
}

void DiffPane::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ShiftModifier) {
        QScrollBar* hbar = horizontalScrollBar();
        const QPoint pixels = event->pixelDelta();
        const QPoint angle = event->angleDelta();
        int delta = 0;
        if (!pixels.isNull()) {
            delta = pixels.x() != 0 ? pixels.x() : pixels.y();
        } else if (!angle.isNull()) {
            const int a = angle.x() != 0 ? angle.x() : angle.y();
            delta = a * hbar->singleStep() / 120;
        }
        if (delta != 0) {
            hbar->setValue(hbar->value() - delta);
            event->accept();
            return;
        }
    }
    QPlainTextEdit::wheelEvent(event);
}

int DiffPane::arrowZoneLeft() const {
    return 4;
}

void DiffPane::setRowPartial(int row, bool selected, bool neutral) {
    if (row < 0 || row >= m_rows.size()) return;
    auto& r = m_rows[row];
    if (r.partialSelected == selected && r.partialNeutral == neutral) return;
    r.partialSelected = selected;
    r.partialNeutral = neutral;
    applyRowBackgrounds();
    m_lineNumberArea->update();
}

int DiffPane::sourceLineAtRow(int row) const {
    if (row < 0 || row >= m_rows.size()) return -1;
    return m_rows[row].sourceLine;
}

bool DiffPane::isRowFiller(int row) const {
    if (row < 0 || row >= m_rows.size()) return false;
    return m_rows[row].filler;
}

void DiffPane::clearAllPartial() {
    bool changed = false;
    for (auto& r : m_rows) {
        if (r.partialSelected || r.partialNeutral) {
            r.partialSelected = false;
            r.partialNeutral = false;
            changed = true;
        }
    }
    if (changed) {
        applyRowBackgrounds();
        m_lineNumberArea->update();
    }
}

void DiffPane::lineNumberAreaMousePressEvent(QMouseEvent* event) {
    const int x = event->position().x();
    const bool inArrow = x < arrowZoneLeft() + kArrowWidth + 2;

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    const int y = event->position().y();
    while (block.isValid()) {
        if (y >= top && y < bottom) {
            if (blockNumber < m_rows.size()) {
                const auto& r = m_rows[blockNumber];
                if (inArrow) {
                    if (r.kind != Diff::Op::Equal || r.partialSelected) {
                        emit arrowClicked(blockNumber);
                    }
                } else if (!r.filler) {
                    const bool shift = event->modifiers() & Qt::ShiftModifier;
                    emit lineNumberClicked(blockNumber, shift);
                }
            }
            return;
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void DiffPane::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), palette().color(QPalette::Window));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    const QPen numberPen(palette().color(QPalette::WindowText).lighter(150));
    const int numX = arrowZoneLeft() + kArrowWidth + kArrowPad;
    const int numWidth = m_lineNumberArea->width() - numX;

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString num;
            bool inBlock = false;
            bool isStart = false;
            bool isEnd = false;
            bool partialSel = false;
            bool partialNeu = false;
            if (blockNumber < m_rows.size()) {
                const auto& r = m_rows[blockNumber];
                if (r.sourceLine >= 0) num = QString::number(r.sourceLine + 1);
                partialSel = r.partialSelected;
                partialNeu = r.partialNeutral;
                inBlock = (r.kind != Diff::Op::Equal) && !partialSel && !partialNeu;
                if (inBlock) {
                    auto isInactive = [&](int idx) {
                        if (idx < 0 || idx >= m_rows.size()) return true;
                        const auto& rr = m_rows[idx];
                        return rr.kind == Diff::Op::Equal || rr.partialSelected ||
                               rr.partialNeutral;
                    };
                    isStart = isInactive(blockNumber - 1);
                    isEnd = isInactive(blockNumber + 1);
                }
            } else {
                num = QString::number(blockNumber + 1);
            }
            painter.setPen(numberPen);
            painter.drawText(numX, top, numWidth - 4, fontMetrics().height(),
                             Qt::AlignRight, num);

            if (blockNumber < m_rows.size() && m_rows[blockNumber].recentlyCopied) {
                const int rowHeight = qRound(blockBoundingRect(block).height());
                const int barX = (m_side == Side::Left)
                                     ? arrowZoneLeft() - 1
                                     : arrowZoneLeft() + kArrowWidth - 2;
                painter.fillRect(QRect(barX, top, 3, rowHeight), arrowColor());
            }

            if (partialSel) {
                const bool prevSel = blockNumber > 0 &&
                                     m_rows[blockNumber - 1].partialSelected;
                const bool nextSel = blockNumber + 1 < m_rows.size() &&
                                     m_rows[blockNumber + 1].partialSelected;
                const bool partialStart = !prevSel;
                const bool partialEnd = !nextSel;
                const bool multi = !(partialStart && partialEnd);

                const int rowHeight = qRound(blockBoundingRect(block).height());
                const int lineX = (m_side == Side::Left)
                                      ? arrowZoneLeft()
                                      : arrowZoneLeft() + kArrowWidth - 1;
                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, true);

                if (multi) {
                    const QPen bluePen(partialArrowEdgeColor(), kBracketPenWidth,
                                       Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
                    painter.setPen(bluePen);
                    int lineTop = top;
                    int lineBottom = top + rowHeight;
                    if (partialStart)
                        lineTop = top + (rowHeight + kArrowHeight) / 2;
                    if (partialEnd) lineBottom = top + rowHeight - 1;
                    painter.drawLine(lineX, lineTop, lineX, lineBottom);

                    if (partialEnd) {
                        const int capLen = kArrowWidth - 2;
                        const int yEnd = top + rowHeight - 1;
                        painter.drawLine(lineX, yEnd, lineX + capLen, yEnd);
                    }
                }

                if (partialStart) {
                    const int ay = top + (rowHeight - kArrowHeight) / 2;
                    const int amid = ay + kArrowHeight / 2;
                    QPolygon arrow;
                    if (m_side == Side::Left) {
                        arrow << QPoint(lineX, ay)
                              << QPoint(lineX + kArrowWidth, amid)
                              << QPoint(lineX, ay + kArrowHeight)
                              << QPoint(lineX + kArrowNotch, amid);
                    } else {
                        arrow << QPoint(lineX, ay)
                              << QPoint(lineX - kArrowWidth, amid)
                              << QPoint(lineX, ay + kArrowHeight)
                              << QPoint(lineX - kArrowNotch, amid);
                    }
                    painter.setBrush(partialArrowColor());
                    painter.setPen(QPen(partialArrowEdgeColor(), 1));
                    painter.drawPolygon(arrow);
                }
                painter.restore();
            }

            if (inBlock) {
                const int rowHeight = qRound(blockBoundingRect(block).height());
                // Bracket line position: line opposite the arrow apex.
                // Left pane: line on LEFT, apex right.  Right pane: mirror.
                const int lineX = (m_side == Side::Left)
                                      ? arrowZoneLeft()
                                      : arrowZoneLeft() + kArrowWidth - 1;

                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, true);

                const QPen bracketPen(bracketColor(), kBracketPenWidth, Qt::SolidLine,
                                      Qt::FlatCap, Qt::MiterJoin);

                // Vertical line: full row height for mid/end rows, only below
                // the arrow for the start row so the arrow head is uncluttered.
                int lineTop = top;
                int lineBottom = top + rowHeight;
                if (isStart) {
                    lineTop = top + (rowHeight + kArrowHeight) / 2;
                }
                if (isEnd) {
                    lineBottom = top + rowHeight - 1;
                }
                painter.setPen(bracketPen);
                painter.drawLine(lineX, lineTop, lineX, lineBottom);

                if (isStart) {
                    const int ay = top + (rowHeight - kArrowHeight) / 2;
                    const int amid = ay + kArrowHeight / 2;
                    QPolygon arrow;
                    if (m_side == Side::Left) {
                        // Barbed arrowhead pointing right.
                        arrow << QPoint(lineX, ay)
                              << QPoint(lineX + kArrowWidth, amid)
                              << QPoint(lineX, ay + kArrowHeight)
                              << QPoint(lineX + kArrowNotch, amid);
                    } else {
                        arrow << QPoint(lineX, ay)
                              << QPoint(lineX - kArrowWidth, amid)
                              << QPoint(lineX, ay + kArrowHeight)
                              << QPoint(lineX - kArrowNotch, amid);
                    }
                    painter.setBrush(arrowColor());
                    painter.setPen(QPen(arrowEdgeColor(), 1));
                    painter.drawPolygon(arrow);
                }
                if (isEnd) {
                    painter.setPen(bracketPen);
                    const int capLen = kArrowWidth - 2;
                    const int yEnd = top + rowHeight - 1;
                    // Hook goes right on both sides (toward the apex on the
                    // left pane, away from it on the right). User preference.
                    if (m_side == Side::Left) {
                        painter.drawLine(lineX, yEnd, lineX + capLen, yEnd);
                    } else {
                        painter.drawLine(lineX, yEnd, lineX + capLen, yEnd);
                    }
                }
                painter.restore();
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
