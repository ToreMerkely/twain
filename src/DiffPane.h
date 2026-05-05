#pragma once

#include <QPlainTextEdit>
#include <QVector>

#include "Diff.h"

class QPaintEvent;
class QResizeEvent;
class QSyntaxHighlighter;

struct DiffRow {
    int sourceLine;   // -1 for filler
    QString text;
    Diff::Op kind;    // Equal, Delete, Insert (filler rows use kind of the surrounding hunk)
    bool filler;
    QVector<Diff::LineSegment> segments;  // intra-line highlighting; empty if not paired
    bool recentlyCopied = false;
};

class DiffPane : public QPlainTextEdit {
    Q_OBJECT
public:
    enum class Side { Left, Right };

    explicit DiffPane(QWidget* parent = nullptr);

    void setSide(Side s);
    void setRows(const QVector<DiffRow>& rows);
    void setLanguageFromPath(const QString& path);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void lineNumberAreaMousePressEvent(QMouseEvent* event);
    int lineNumberAreaWidth() const;
    int arrowZoneLeft() const;

signals:
    void arrowClicked(int row);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    void applyRowBackgrounds();

    QWidget* m_lineNumberArea;
    QVector<DiffRow> m_rows;
    QSyntaxHighlighter* m_highlighter = nullptr;
    Side m_side = Side::Left;
};
