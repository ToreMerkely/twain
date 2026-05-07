#include "DiffOverview.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

DiffOverview::DiffOverview(QWidget* parent) : QWidget(parent) {
    setFixedWidth(14);
    setCursor(Qt::PointingHandCursor);
}

QSize DiffOverview::sizeHint() const { return QSize(14, 100); }

void DiffOverview::setTotalRows(int n) {
    if (m_totalRows == n) return;
    m_totalRows = n;
    update();
}

void DiffOverview::setMarks(const QVector<Mark>& marks) {
    m_marks = marks;
    update();
}

void DiffOverview::setViewport(int topRow, int visibleRows) {
    if (m_viewportTop == topRow && m_viewportRows == visibleRows) return;
    m_viewportTop = topRow;
    m_viewportRows = visibleRows;
    update();
}

void DiffOverview::setCurrentRow(int row) {
    if (m_currentRow == row) return;
    m_currentRow = row;
    update();
}

void DiffOverview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());

    if (m_totalRows <= 0) return;
    const int h = height();
    const double rowsPerPx = static_cast<double>(m_totalRows) / static_cast<double>(h);

    auto rowToY = [&](int row) {
        return static_cast<int>(row / rowsPerPx);
    };

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(220, 80, 80));
    for (const auto& m : m_marks) {
        const int y0 = rowToY(m.rowStart);
        const int y1 = rowToY(m.rowEnd);
        const int hh = std::max(2, y1 - y0);
        p.drawRect(1, y0, width() - 2, hh);
    }

    if (m_viewportRows > 0) {
        const int vy0 = rowToY(m_viewportTop);
        const int vy1 = rowToY(m_viewportTop + m_viewportRows);
        QRect vp(0, vy0, width() - 1, std::max(2, vy1 - vy0));
        p.setBrush(QColor(80, 120, 200, 60));
        p.setPen(QColor(80, 120, 200, 180));
        p.drawRect(vp);
    }
}

int DiffOverview::rowFromY(int y) const {
    if (m_totalRows <= 0 || height() <= 0) return 0;
    const double rowsPerPx = static_cast<double>(m_totalRows) / static_cast<double>(height());
    int row = static_cast<int>(y * rowsPerPx);
    return std::clamp(row, 0, m_totalRows - 1);
}

void DiffOverview::emitScrollForY(int y) {
    const int row = rowFromY(y);
    int top = row - m_viewportRows / 2;
    top = std::clamp(top, 0, std::max(0, m_totalRows - 1));
    emit scrollRequested(top);
}

void DiffOverview::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emitScrollForY(e->position().y());
}

void DiffOverview::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) emitScrollForY(e->position().y());
}
