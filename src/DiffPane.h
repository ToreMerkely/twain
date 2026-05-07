#pragma once

#include <QPlainTextEdit>
#include <QVector>

#include "Diff.h"

class QPaintEvent;
class QResizeEvent;
class QSyntaxHighlighter;
class QWheelEvent;

struct DiffRow {
    int sourceLine;   // -1 for filler
    QString text;
    Diff::Op kind;    // Equal, Delete, Insert (filler rows use kind of the surrounding hunk)
    bool filler;
    QVector<Diff::LineSegment> segments;  // intra-line highlighting; empty if not paired
    bool recentlyCopied = false;
    bool partialSelected = false;  // user picked this row for single/multi-line copy
    bool partialNeutral = false;   // row is in a partial-selected block but not picked
};

class DiffPane : public QPlainTextEdit {
    Q_OBJECT
public:
    enum class Side { Left, Right };

    explicit DiffPane(QWidget* parent = nullptr);

    void setSide(Side s);
    void setRows(const QVector<DiffRow>& rows);
    void setLanguageFromPath(const QString& path);
    QStringList extractContent() const;

    struct CursorContext {
        int fileLine = -1;
        int column = 0;
    };
    CursorContext saveCursor() const;
    void restoreCursor(CursorContext ctx);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void lineNumberAreaMousePressEvent(QMouseEvent* event);
    int lineNumberAreaWidth() const;
    int arrowZoneLeft() const;

    void setRowPartial(int row, bool selected, bool neutral);
    void clearAllPartial();
    int sourceLineAtRow(int row) const;
    bool isRowFiller(int row) const;

signals:
    void arrowClicked(int row);
    void lineNumberClicked(int row);
    void contentEdited();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    void applyRowBackgrounds();

    QWidget* m_lineNumberArea;
    QVector<DiffRow> m_rows;
    QSyntaxHighlighter* m_highlighter = nullptr;
    Side m_side = Side::Left;
    bool m_loading = false;
};
