#pragma once

#include <QPair>
#include <QPlainTextEdit>
#include <QVector>

#include "Diff.h"

class QPaintEvent;
class QResizeEvent;
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
    bool isTruncationMarker = false;  // clickable "load more" row at end of truncated file
};

class DiffPane : public QPlainTextEdit {
    Q_OBJECT
public:
    enum class Side { Left, Right };

    explicit DiffPane(QWidget* parent = nullptr);

    void setSide(Side s);
    void setRows(const QVector<DiffRow>& rows);
    QStringList extractContent() const;

    void setSearchTerm(const QString& term);
    bool findNext();
    bool findPrev();
    int matchCount() const { return m_matches.size(); }
    int currentMatchIndex() const { return m_currentMatch; }

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
    bool isCursorOnTruncationMarker() const;

signals:
    void arrowClicked(int row);
    void lineNumberClicked(int row, bool shift);
    void clearPartialRequested();
    void contentEdited();
    void truncationMarkerClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    void applyRowBackgrounds();
    void applyBlockBackgroundForRow(int row);

    QWidget* m_lineNumberArea;
    QVector<DiffRow> m_rows;
    Side m_side = Side::Left;
    bool m_loading = false;
    int m_maxSourceLine = 0;  // cached: largest sourceLine in m_rows + 1
    QString m_searchTerm;
    QVector<QPair<int, int>> m_matches;  // (absolute position, length) within the document
    int m_currentMatch = -1;

    void scrollToCurrentMatch();
};
