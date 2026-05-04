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
        if (r.kind == Diff::Op::Equal && !r.filler) continue;

        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(colorFor(r.kind, r.filler));
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        QTextBlock block = doc->findBlockByNumber(i);
        if (!block.isValid()) continue;
        sel.cursor = QTextCursor(block);
        sel.cursor.clearSelection();
        selections.append(sel);
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
    return 8 + fontMetrics().horizontalAdvance('9') * digits;
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

    painter.setPen(palette().color(QPalette::WindowText).lighter(150));

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString num;
            if (blockNumber < m_rows.size() && m_rows[blockNumber].sourceLine >= 0) {
                num = QString::number(m_rows[blockNumber].sourceLine + 1);
            } else if (blockNumber < m_rows.size()) {
                num = QString();  // filler
            } else {
                num = QString::number(blockNumber + 1);
            }
            painter.drawText(0, top, m_lineNumberArea->width() - 4,
                             fontMetrics().height(), Qt::AlignRight, num);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
