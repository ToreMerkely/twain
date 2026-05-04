#include "DiffPane.h"

#include <QColor>
#include <QFont>
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
QColor arrowColor() { return QColor(245, 195, 0); }
constexpr int kArrowWidth = 10;
constexpr int kArrowHeight = 10;
constexpr int kArrowPad = 4;

}  // namespace

DiffPane::DiffPane(QWidget* parent) : QPlainTextEdit(parent) {
    QFont font("JetBrains Mono", 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setReadOnly(true);

    m_lineNumberArea = new LineNumberArea(this);

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

void DiffPane::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
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
            bool drawArrow = false;
            if (blockNumber < m_rows.size()) {
                const auto& r = m_rows[blockNumber];
                if (r.sourceLine >= 0) num = QString::number(r.sourceLine + 1);
                drawArrow = (r.kind != Diff::Op::Equal) && !r.filler;
            } else {
                num = QString::number(blockNumber + 1);
            }
            painter.setPen(numberPen);
            painter.drawText(0, top, numWidth - 4, fontMetrics().height(),
                             Qt::AlignRight, num);
            if (drawArrow) {
                const int rowHeight = qRound(blockBoundingRect(block).height());
                const int ax = m_lineNumberArea->width() - kArrowWidth - 1;
                const int ay = top + (rowHeight - kArrowHeight) / 2;
                QPolygon tri;
                if (m_side == Side::Left) {
                    // Right-pointing triangle: would copy left -> right
                    tri << QPoint(ax, ay)
                        << QPoint(ax + kArrowWidth, ay + kArrowHeight / 2)
                        << QPoint(ax, ay + kArrowHeight);
                } else {
                    // Left-pointing triangle: would copy right -> left
                    tri << QPoint(ax + kArrowWidth, ay)
                        << QPoint(ax, ay + kArrowHeight / 2)
                        << QPoint(ax + kArrowWidth, ay + kArrowHeight);
                }
                painter.setBrush(arrowColor());
                painter.setPen(Qt::NoPen);
                painter.drawPolygon(tri);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
