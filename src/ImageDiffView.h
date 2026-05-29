#pragma once

#include <QString>
#include <QWidget>

class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsView;
class QLabel;
class QSplitter;

// Side-by-side image viewer used when both sides of a pair are recognized
// image files. Mouse drag pans, Ctrl+wheel zooms. The two panes' viewports
// are kept in sync: zooming one zooms both, scrolling one scrolls both.
class ImageDiffView : public QWidget {
    Q_OBJECT
public:
    explicit ImageDiffView(QWidget* parent = nullptr);

    // Loads both images. Returns false (and fills *error) if either fails to
    // decode; the caller can then drop the tab.
    bool setFiles(const QString& leftPath, const QString& rightPath,
                  QString* error = nullptr);

    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }

    // Tells the caller whether a given path is one we'll handle as an
    // image. Centralized so MainWindow doesn't need to know the list.
    static bool looksLikeImage(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void fitInView();
    void applyZoom(double factor, QGraphicsView* pivotView);

    QSplitter* m_splitter = nullptr;
    QGraphicsView* m_left = nullptr;
    QGraphicsView* m_right = nullptr;
    QGraphicsScene* m_leftScene = nullptr;
    QGraphicsScene* m_rightScene = nullptr;
    QGraphicsPixmapItem* m_leftItem = nullptr;
    QGraphicsPixmapItem* m_rightItem = nullptr;
    QLabel* m_leftLabel = nullptr;
    QLabel* m_rightLabel = nullptr;
    QString m_leftPath;
    QString m_rightPath;
    bool m_syncing = false;
    // Set when setFiles loaded new images; we wait until the widget is
    // actually shown (and laid out) before computing the fit, otherwise
    // the viewport size is still 0x0.
    bool m_fitPending = false;
};
