#include "ImageDiffView.h"

#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

// Centralized list of extensions we treat as image files. SVG support
// requires the Qt6Svg module which is optional; if the build doesn't
// have it, .svg files just won't decode and the caller falls back.
const QSet<QString>& imageExtensions() {
    static const QSet<QString> exts = {
        "png", "jpg", "jpeg", "gif", "bmp", "webp",
        "tif", "tiff", "ico", "svg",
    };
    return exts;
}

QWidget* makeImagePane(QGraphicsView*& outView, QGraphicsScene*& outScene,
                       QGraphicsPixmapItem*& outItem, QLabel*& outLabel,
                       QWidget* parent) {
    auto* container = new QWidget(parent);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    outLabel = new QLabel(container);
    outLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outLabel->setContentsMargins(4, 2, 4, 2);
    outLabel->setTextFormat(Qt::PlainText);
    outLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont labelFont = outLabel->font();
    labelFont.setPointSizeF(labelFont.pointSizeF() * 1.3);
    outLabel->setFont(labelFont);

    outScene = new QGraphicsScene(container);
    outItem = outScene->addPixmap(QPixmap());
    outView = new QGraphicsView(outScene, container);
    outView->setDragMode(QGraphicsView::ScrollHandDrag);
    outView->setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    outView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    outView->setBackgroundBrush(Qt::black);
    outView->setFrameStyle(QFrame::NoFrame);

    lay->addWidget(outLabel);
    lay->addWidget(outView, 1);
    return container;
}

}  // namespace

bool ImageDiffView::looksLikeImage(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return imageExtensions().contains(ext);
}

ImageDiffView::ImageDiffView(QWidget* parent) : QWidget(parent) {
    m_splitter = new QSplitter(Qt::Horizontal, this);

    QWidget* leftPane  = makeImagePane(m_left,  m_leftScene,  m_leftItem,  m_leftLabel,  m_splitter);
    QWidget* rightPane = makeImagePane(m_right, m_rightScene, m_rightItem, m_rightLabel, m_splitter);
    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(rightPane);
    m_splitter->setSizes({1, 1});

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);

    // Sync scrollbars between panes. Wheel events on either viewport go
    // through eventFilter so we can intercept Ctrl+wheel for zoom.
    auto syncScroll = [this](QGraphicsView* src, QGraphicsView* dst) {
        connect(src->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this, src, dst](int v) {
                    if (m_syncing) return;
                    m_syncing = true;
                    dst->horizontalScrollBar()->setValue(v);
                    m_syncing = false;
                });
        connect(src->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this, src, dst](int v) {
                    if (m_syncing) return;
                    m_syncing = true;
                    dst->verticalScrollBar()->setValue(v);
                    m_syncing = false;
                });
    };
    syncScroll(m_left, m_right);
    syncScroll(m_right, m_left);

    m_left->viewport()->installEventFilter(this);
    m_right->viewport()->installEventFilter(this);
}

bool ImageDiffView::setFiles(const QString& leftPath, const QString& rightPath,
                             QString* error) {
    auto load = [error](const QString& path, QPixmap* out) -> bool {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (img.isNull()) {
            if (error) *error = QString("Could not decode image %1: %2")
                                  .arg(path, reader.errorString());
            return false;
        }
        *out = QPixmap::fromImage(img);
        return true;
    };
    QPixmap lp, rp;
    if (!load(leftPath, &lp))  return false;
    if (!load(rightPath, &rp)) return false;

    m_leftPath = leftPath;
    m_rightPath = rightPath;
    m_leftItem->setPixmap(lp);
    m_rightItem->setPixmap(rp);
    m_leftScene->setSceneRect(lp.rect());
    m_rightScene->setSceneRect(rp.rect());
    m_leftLabel->setText(leftPath);
    m_leftLabel->setToolTip(QString("%1\n%2x%3 px").arg(leftPath)
                                .arg(lp.width()).arg(lp.height()));
    m_rightLabel->setText(rightPath);
    m_rightLabel->setToolTip(QString("%1\n%2x%3 px").arg(rightPath)
                                 .arg(rp.width()).arg(rp.height()));

    // Wait until showEvent fires (and the views have a real size) before
    // computing the fit. If we fit now the viewports are still 0x0 and the
    // scale is way off.
    m_fitPending = true;
    if (isVisible()) fitInView();  // already visible, e.g. switching pairs
    return true;
}

void ImageDiffView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (m_fitPending) fitInView();
}

void ImageDiffView::fitInView() {
    // Bail if the viewport isn't laid out yet; the showEvent path will
    // call us again once it is.
    if (m_left->viewport()->width() <= 0 || m_left->viewport()->height() <= 0) {
        return;
    }
    m_left->resetTransform();
    m_right->resetTransform();
    m_left->fitInView(m_leftItem,  Qt::KeepAspectRatio);
    m_right->fitInView(m_rightItem, Qt::KeepAspectRatio);
    m_fitPending = false;
}

void ImageDiffView::applyZoom(double factor, QGraphicsView* pivotView) {
    // Apply the same scale to both views so they stay in lockstep. The
    // pivot's AnchorUnderMouse keeps the point under the cursor stable.
    pivotView->scale(factor, factor);
    QGraphicsView* other = (pivotView == m_left) ? m_right : m_left;
    other->scale(factor, factor);
}

bool ImageDiffView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            QGraphicsView* view =
                (watched == m_left->viewport())  ? m_left  :
                (watched == m_right->viewport()) ? m_right : nullptr;
            if (view) {
                const double f = (we->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
                applyZoom(f, view);
                return true;  // consume — don't also scroll
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
