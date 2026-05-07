#pragma once

#include <QVector>
#include <QWidget>

class DiffOverview : public QWidget {
    Q_OBJECT
public:
    explicit DiffOverview(QWidget* parent = nullptr);

    struct Mark {
        int rowStart = 0;
        int rowEnd = 0;
    };

    void setTotalRows(int n);
    void setMarks(const QVector<Mark>& marks);
    void setViewport(int topRow, int visibleRows);
    void setCurrentRow(int row);

signals:
    void scrollRequested(int topRow);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    int m_totalRows = 0;
    QVector<Mark> m_marks;
    int m_viewportTop = 0;
    int m_viewportRows = 0;
    int m_currentRow = -1;

    int rowFromY(int y) const;
    void emitScrollForY(int y);
};
