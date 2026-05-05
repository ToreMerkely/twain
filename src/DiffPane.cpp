#include "DiffPane.h"

#include <QColor>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSyntaxHighlighter>

#include "HighlighterFactory.h"
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTextBlock>
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

QColor segmentColor() { return QColor(255, 150, 150); }
QColor arrowColor() { return QColor(255, 175, 30); }
QColor arrowEdgeColor() { return QColor(160, 100, 0); }
constexpr int kArrowWidth = 12;
constexpr int kArrowHeight = 12;
constexpr int kArrowPad = 6;
constexpr int kBracketPenWidth = 1;

}  // namespace

DiffPane::DiffPane(QWidget* parent) : QPlainTextEdit(parent) {
    QFont font("JetBrains Mono", 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setReadOnly(true);

    m_lineNumberArea = new LineNumberArea(this);

    // Keep editable so the text cursor blinks, but filter out modifying
    // keys in keyPressEvent. Phase B will lift this and accept typing.
    setReadOnly(false);
    setUndoRedoEnabled(false);
    setCursorWidth(2);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &DiffPane::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &DiffPane::updateLineNumberArea);

    updateLineNumberAreaWidth();
}

void DiffPane::setSide(Side s) {
    m_side = s;
    updateLineNumberAreaWidth();
    m_lineNumberArea->update();
}

void DiffPane::setLanguageFromPath(const QString& path) {
    delete m_highlighter;
    m_highlighter = makeHighlighter(path, document());
}

void DiffPane::setRows(const QVector<DiffRow>& rows) {
    m_rows = rows;
    QStringList texts;
    texts.reserve(rows.size());
    for (const auto& r : rows) texts << r.text;
    setPlainText(texts.join('\n'));
    applyRowBackgrounds();
    m_lineNumberArea->update();
}

void DiffPane::applyRowBackgrounds() {
    QList<QTextEdit::ExtraSelection> selections;
    selections.reserve(m_rows.size());

    QTextDocument* doc = document();
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto& r = m_rows[i];
        QTextBlock block = doc->findBlockByNumber(i);
        if (!block.isValid()) continue;

        if (r.kind != Diff::Op::Equal || r.filler) {
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(colorFor(r.kind, r.filler));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block);
            sel.cursor.clearSelection();
            selections.append(sel);
        }

        for (const auto& seg : r.segments) {
            if (!seg.differ) continue;
            QTextEdit::ExtraSelection ssel;
            ssel.format.setBackground(segmentColor());
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
    int max = qMax(1, blockCount());
    for (const auto& r : m_rows) {
        if (r.sourceLine + 1 > max) max = r.sourceLine + 1;
    }
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

void DiffPane::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Home:
        case Qt::Key_End:
            QPlainTextEdit::keyPressEvent(event);
            return;
    }
    if (event->matches(QKeySequence::Copy) ||
        event->matches(QKeySequence::SelectAll)) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    // Anything that would modify text — drop. Let parent handle shortcuts.
    event->ignore();
}

void DiffPane::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

int DiffPane::arrowZoneLeft() const {
    return m_lineNumberArea->width() - kArrowWidth - 1;
}

void DiffPane::lineNumberAreaMousePressEvent(QMouseEvent* event) {
    const int x = event->position().x();
    if (x < arrowZoneLeft() - 2) return;  // not in arrow column

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    const int y = event->position().y();
    while (block.isValid()) {
        if (y >= top && y < bottom) {
            if (blockNumber < m_rows.size()) {
                const auto& r = m_rows[blockNumber];
                if (r.kind != Diff::Op::Equal && !r.filler) {
                    emit arrowClicked(blockNumber);
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
    const int numWidth = m_lineNumberArea->width() - kArrowWidth - kArrowPad;

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString num;
            bool inBlock = false;
            bool isStart = false;
            bool isEnd = false;
            if (blockNumber < m_rows.size()) {
                const auto& r = m_rows[blockNumber];
                if (r.sourceLine >= 0) num = QString::number(r.sourceLine + 1);
                inBlock = (r.kind != Diff::Op::Equal);
                if (inBlock) {
                    const bool prevEqual = blockNumber == 0 ||
                                           m_rows[blockNumber - 1].kind == Diff::Op::Equal;
                    const bool nextEqual = blockNumber + 1 >= m_rows.size() ||
                                           m_rows[blockNumber + 1].kind == Diff::Op::Equal;
                    isStart = prevEqual;
                    isEnd = nextEqual;
                }
            } else {
                num = QString::number(blockNumber + 1);
            }
            painter.setPen(numberPen);
            painter.drawText(0, top, numWidth - 4, fontMetrics().height(),
                             Qt::AlignRight, num);

            if (blockNumber < m_rows.size() && m_rows[blockNumber].recentlyCopied) {
                const int rowHeight = qRound(blockBoundingRect(block).height());
                const int barX = (m_side == Side::Left)
                                     ? arrowZoneLeft() - 1
                                     : arrowZoneLeft() + kArrowWidth - 2;
                painter.fillRect(QRect(barX, top, 3, rowHeight), arrowColor());
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

                const QPen bracketPen(arrowColor(), kBracketPenWidth, Qt::SolidLine,
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
                    QPolygon tri;
                    if (m_side == Side::Left) {
                        tri << QPoint(lineX, ay)
                            << QPoint(lineX + kArrowWidth, ay + kArrowHeight / 2)
                            << QPoint(lineX, ay + kArrowHeight);
                    } else {
                        tri << QPoint(lineX, ay)
                            << QPoint(lineX - kArrowWidth, ay + kArrowHeight / 2)
                            << QPoint(lineX, ay + kArrowHeight);
                    }
                    painter.setBrush(arrowColor());
                    painter.setPen(QPen(arrowEdgeColor(), 1));
                    painter.drawPolygon(tri);
                }
                if (isEnd) {
                    painter.setPen(bracketPen);
                    const int capLen = kArrowWidth - 2;
                    const int yEnd = top + rowHeight - 1;
                    if (m_side == Side::Left) {
                        painter.drawLine(lineX, yEnd, lineX + capLen, yEnd);
                    } else {
                        painter.drawLine(lineX, yEnd, lineX - capLen, yEnd);
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
