#include "ImageCanvas.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

static constexpr double VERTEX_HIT_PX = 9.0;

ImageCanvas::ImageCanvas(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(400, 300);

    resetBtn_ = new QToolButton(this);
    resetBtn_->setCursor(Qt::PointingHandCursor);
    resetBtn_->setFocusPolicy(Qt::NoFocus);
    resetBtn_->setStyleSheet("QToolButton { background: rgba(20,22,26,0.85); color: #e8eaed;"
                             " border: 1px solid rgba(255,255,255,0.18); border-radius: 12px;"
                             " padding: 5px 14px; font-size: 12px; }"
                             "QToolButton:hover { background: rgba(45,48,54,0.95);"
                             " border-color: rgba(74,163,255,0.8); }");
    resetBtn_->hide();
    connect(resetBtn_, &QToolButton::clicked, this, &ImageCanvas::resetView);
}

void ImageCanvas::reorder(const QVector<int>& order) {
    if (order.size() != sel_.size()) return;
    QVector<Sel> reordered;
    reordered.reserve(sel_.size());
    for (int idx : order) reordered.push_back(sel_[idx]);
    sel_ = reordered;
    highlight_ = -1;
    update();
}

void ImageCanvas::setImage(const QImage& img) {
    image_ = img;
    scaledCache_ = QImage();
    cachedScale_ = -1.0;
    sel_.clear();
    polyInProgress_.clear();
    undoStack_.clear();
    highlight_ = -1;
    haveCursor_ = false;
    zoom_ = 1.0;
    pan_ = {0, 0};
    updateResetButton();
    recomputeTransform();
    update();
    emit selectionsChanged();
}

void ImageCanvas::setMode(Mode m) {
    mode_ = m;
    polyInProgress_.clear();
    dragging_ = false;
    update();
}

void ImageCanvas::clearSelections() {
    sel_.clear();
    polyInProgress_.clear();
    undoStack_.clear();
    highlight_ = -1;
    update();
    emit selectionsChanged();
}

void ImageCanvas::undo()
{
    if (!polyInProgress_.isEmpty()) {
        polyInProgress_.removeLast();
        update();
        return;
    }
    if (!undoStack_.isEmpty()) {
        sel_ = undoStack_.takeLast();
        if (highlight_ >= sel_.size())
            highlight_ = -1;
        update();
        emit selectionsChanged();
    }
}

void ImageCanvas::setHighlight(int index) { highlight_ = index; update(); }

void ImageCanvas::addQuadSelection(const QPolygonF& quad) {
    if (quad.size() < 3) return;
    undoSnapshot();
    Sel s;
    s.poly = quad;
    s.id = nextSelId_++;
    sel_.push_back(s);
    update();
    emit selectionsChanged();
}

int ImageCanvas::selectionAtWidgetPoint(const QPointF& widgetPt) const {
    return hitTestSelection(widgetPt);
}

void ImageCanvas::setSelectionState(int index, bool confirmed, const QString& label) {
    if (index < 0 || index >= sel_.size()) return;
    sel_[index].confirmed = confirmed;
    sel_[index].label     = label;
    update();
}

void ImageCanvas::recomputeTransform()
{
    if (image_.isNull()) {
        scale_ = 1.0;
        offset_ = {0, 0};
        return;
    }
    const double fit = std::min((double) width() / image_.width(),
                                (double) height() / image_.height());
    scale_ = fit * zoom_;
    const QPointF centered((width() - image_.width() * scale_) / 2.0,
                           (height() - image_.height() * scale_) / 2.0);
    offset_ = centered + pan_;
}

void ImageCanvas::clampPan()
{
    if (image_.isNull()) {
        pan_ = {0, 0};
        return;
    }
    const double fit = std::min((double) width() / image_.width(),
                                (double) height() / image_.height());
    const double w = image_.width() * fit * zoom_;
    const double h = image_.height() * fit * zoom_;
    const double maxX = std::max(0.0, (w - width()) / 2.0);
    const double maxY = std::max(0.0, (h - height()) / 2.0);
    pan_.setX(std::clamp(pan_.x(), -maxX, maxX));
    pan_.setY(std::clamp(pan_.y(), -maxY, maxY));
}

void ImageCanvas::resetView()
{
    zoom_ = 1.0;
    pan_ = {0, 0};
    recomputeTransform();
    updateResetButton();
    update();
}

void ImageCanvas::updateResetButton()
{
    if (!resetBtn_)
        return;
    const bool zoomed = zoom_ > 1.0;
    resetBtn_->setVisible(zoomed);
    if (!zoomed)
        return;
    resetBtn_->setText(QString("%1%  ·  Reset view").arg(qRound(zoom_ * 100)));
    resetBtn_->adjustSize();
    resetBtn_->move((width() - resetBtn_->width()) / 2, height() - resetBtn_->height() - 12);
    resetBtn_->raise();
}

void ImageCanvas::wheelEvent(QWheelEvent *e)
{
    if (image_.isNull()) {
        e->ignore();
        return;
    }
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) {
        e->ignore();
        return;
    }

    const QPointF cursor = e->position();
    const QPointF imgPt = toImage(cursor);

    zoom_ = std::clamp(zoom_ * std::pow(1.2, steps), 1.0, 12.0);
    if (zoom_ < 1.01)
        zoom_ = 1.0;

    pan_ = {0, 0};
    recomputeTransform();
    pan_ = cursor - toWidget(imgPt);
    clampPan();
    recomputeTransform();

    updateResetButton();
    update();
    e->accept();
}

void ImageCanvas::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    clampPan();
    recomputeTransform();
    updateResetButton();
}

QPointF ImageCanvas::toImage(const QPointF& p) const {
    return QPointF((p.x() - offset_.x()) / scale_, (p.y() - offset_.y()) / scale_);
}

QPointF ImageCanvas::toWidget(const QPointF& p) const {
    return QPointF(p.x() * scale_ + offset_.x(), p.y() * scale_ + offset_.y());
}

int ImageCanvas::hitTestSelection(const QPointF& widgetPt) const {
    QPointF ip = toImage(widgetPt);
    for (int i = sel_.size() - 1; i >= 0; --i)
        if (sel_[i].poly.containsPoint(ip, Qt::OddEvenFill)) return i;
    return -1;
}

bool ImageCanvas::hitTestVertex(const QPointF& widgetPt, int& selIdx, int& vertIdx) const {
    for (int i = sel_.size() - 1; i >= 0; --i) {
        const QPolygonF& p = sel_[i].poly;
        for (int v = 0; v < p.size(); ++v) {
            QPointF wp = toWidget(p[v]);
            if (QLineF(wp, widgetPt).length() <= VERTEX_HIT_PX) { selIdx = i; vertIdx = v; return true; }
        }
    }
    return false;
}

void ImageCanvas::finishPolygon() {
    if (polyInProgress_.size() >= 3) {
        undoSnapshot();
        Sel s;
        s.poly = polyInProgress_;
        s.id = nextSelId_++;
        sel_.push_back(s);
        polyInProgress_.clear();
        update();
        emit selectionsChanged();
    } else {
        polyInProgress_.clear();
        update();
    }
}

void ImageCanvas::undoSnapshot()
{
    undoStack_.push_back(sel_);
    if (undoStack_.size() > 50)
        undoStack_.removeFirst();
}

void ImageCanvas::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.fillRect(rect(), QColor(30, 30, 30));
    if (image_.isNull()) {
        g.setPen(Qt::gray);
        g.drawText(rect(), Qt::AlignCenter, "Open an image to begin");
        return;
    }
    recomputeTransform();

    if (zoom_ == 1.0) {
        const int tw = int(image_.width() * scale_);
        const int th = int(image_.height() * scale_);
        if (scaledCache_.isNull() || cachedScale_ != scale_ || cachedSize_ != QSize(tw, th)) {
            scaledCache_ = image_.scaled(tw, th, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            cachedScale_ = scale_;
            cachedSize_ = QSize(tw, th);
        }
        g.drawImage(QPointF(offset_.x(), offset_.y()), scaledCache_);
    } else {
        g.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF imageRect(offset_, QSizeF(image_.width() * scale_, image_.height() * scale_));
        const QRectF target = imageRect.intersected(QRectF(rect()));
        const QRectF source(toImage(target.topLeft()), toImage(target.bottomRight()));
        g.drawImage(target, image_, source);
    }

    for (int i = 0; i < sel_.size(); ++i) {
        QPolygonF wp;
        for (const QPointF& ip : sel_[i].poly) wp << toWidget(ip);

        const bool hi = (i == highlight_);
        QColor base = sel_[i].confirmed ? QColor(70, 220, 120) : QColor(40, 190, 255);
        QColor col  = hi ? QColor(255, 205, 40) : base;

        g.setPen(QPen(col, hi ? 3 : 2));
        g.setBrush(QColor(col.red(), col.green(), col.blue(), hi ? 55 : 28));
        g.drawPolygon(wp);

        g.setBrush(col);
        for (const QPointF& p : wp) g.drawEllipse(p, 3.5, 3.5);
    }

    if (dragging_ && mode_ == Rectangle) {
        QRectF r(toWidget(dragStartImg_), toWidget(dragCurImg_));
        g.setPen(QPen(Qt::white, 1, Qt::DashLine));
        g.setBrush(QColor(255, 255, 255, 25));
        g.drawRect(r.normalized());
    }

    if (!polyInProgress_.isEmpty()) {
        QPolygonF wp;
        for (const QPointF& ip : polyInProgress_) wp << toWidget(ip);
        g.setPen(QPen(QColor(255, 255, 255), 2));
        g.setBrush(Qt::NoBrush);
        g.drawPolyline(wp);

        if (haveCursor_) {
            QPointF cur = toWidget(cursorImg_);
            g.setPen(QPen(QColor(255, 255, 255, 170), 1, Qt::DashLine));
            g.drawLine(wp.back(), cur);
            if (wp.size() >= 2)
                g.drawLine(cur, wp.front());

            if (wp.size() >= 2) {
                QPolygonF prev = wp; prev << cur;
                g.setPen(Qt::NoPen);
                g.setBrush(QColor(255, 255, 255, 30));
                g.drawPolygon(prev);
            }
        }
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(255, 120, 60));
        for (int i = 0; i < wp.size(); ++i)
            g.drawEllipse(wp[i], i == 0 ? 5.0 : 3.5, i == 0 ? 5.0 : 3.5);
    }
}

void ImageCanvas::mousePressEvent(QMouseEvent* e) {
    if (image_.isNull()) return;

    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        panLast_ = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    const QPointF ip = toImage(e->position());

    if (e->button() == Qt::LeftButton) {
        if (mode_ == ClickOnly) {
            emit canvasClickedImagePoint(ip);
            return;
        }

        emit canvasClickedImagePoint(ip);

        int si, vi;
        if (mode_ == Hand && polyInProgress_.isEmpty() && hitTestVertex(e->position(), si, vi)) {
            undoSnapshot();
            dragSel_ = si; dragVert_ = vi;
            return;
        }

        if (polyInProgress_.isEmpty()) {
            int hit = hitTestSelection(e->position());
            if (hit >= 0 && mode_ == Hand) {
                highlight_ = hit;
                update();
                emit selectionClicked(hit);
                return;
            }
        }

        if (mode_ == Rectangle) {
            dragging_ = true;
            dragStartImg_ = dragCurImg_ = ip;
        } else if (mode_ == Polygon) {
            if (polyInProgress_.size() >= 3 &&
                QLineF(toWidget(polyInProgress_.first()), e->position()).length() <= VERTEX_HIT_PX + 2) {
                finishPolygon();
                return;
            }
            polyInProgress_ << ip;
        }
        update();
    }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        pan_ += e->position() - panLast_;
        panLast_ = e->position();
        clampPan();
        recomputeTransform();
        update();
        return;
    }

    cursorImg_ = toImage(e->position());
    haveCursor_ = true;

    if (dragSel_ >= 0 && dragVert_ >= 0 && mode_ == Hand) {
        sel_[dragSel_].poly[dragVert_] = cursorImg_;
        update();
        return;
    }
    if (dragging_ && mode_ == Rectangle)
        dragCurImg_ = cursorImg_;
    if (!polyInProgress_.isEmpty() || dragging_)
        update();
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (panning_ && e->button() == Qt::MiddleButton) {
        panning_ = false;
        unsetCursor();
        return;
    }
    if (dragSel_ >= 0) {
        int moved = dragSel_;
        dragSel_ = dragVert_ = -1;
        emit selectionGeometryChanged(moved);
        update();
        return;
    }
    if (dragging_ && mode_ == Rectangle && e->button() == Qt::LeftButton) {
        dragging_ = false;
        QRectF r = QRectF(dragStartImg_, dragCurImg_).normalized();
        if (r.width() > 4 && r.height() > 4) {
            undoSnapshot();
            QPolygonF poly;
            poly << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
            Sel s;
            s.poly = poly;
            s.id = nextSelId_++;
            sel_.push_back(s);
            emit selectionsChanged();
        }
        update();
    }
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (mode_ == Polygon && !polyInProgress_.isEmpty()) { finishPolygon(); return; }
    QWidget::mouseDoubleClickEvent(e);
}

void ImageCanvas::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    if (!polyInProgress_.isEmpty()) {
        QAction* undoPt = menu.addAction("Remove last point (Ctrl+Z)");
        QAction* fin    = menu.addAction("Finish polygon (Enter)");
        QAction* cancel = menu.addAction("Cancel polygon (Esc)");
        QAction* a = menu.exec(e->globalPos());
        if (a == undoPt) { if (!polyInProgress_.isEmpty()) polyInProgress_.removeLast(); update(); }
        else if (a == fin)    finishPolygon();
        else if (a == cancel) { polyInProgress_.clear(); update(); }
        return;
    }
    int si, vi;
    int hit = hitTestSelection(e->pos());
    bool onVertex = hitTestVertex(e->pos(), si, vi);
    if (onVertex || hit >= 0) {
        int target = onVertex ? si : hit;
        QAction* delVert = onVertex && sel_[target].poly.size() > 3
                           ? menu.addAction("Delete this point") : nullptr;
        QAction* delSel  = menu.addAction(QString("Delete selection %1").arg(target + 1));
        QAction* a = menu.exec(e->globalPos());
        if (delVert && a == delVert) {
            undoSnapshot();
            sel_[target].poly.remove(vi);
            emit selectionGeometryChanged(target);
            update();
        } else if (a == delSel) {
            undoSnapshot();
            sel_.remove(target);
            if (highlight_ >= sel_.size()) highlight_ = -1;
            update();
            emit selectionsChanged();
        }
    }
}

void ImageCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (e->key() == Qt::Key_0 && (e->modifiers() & Qt::ControlModifier)) {
        resetView();
        return;
    }
    switch (e->key()) {
    case Qt::Key_Return: case Qt::Key_Enter:
        if (mode_ == Polygon) finishPolygon();
        break;
    case Qt::Key_Escape:
        polyInProgress_.clear(); update();
        break;
    case Qt::Key_Backspace: case Qt::Key_Delete:
        if (!polyInProgress_.isEmpty()) {
            polyInProgress_.removeLast();
            update();
            break;
        }
        if (highlight_ < 0 || highlight_ >= sel_.size())
            break;
        undoSnapshot();
        sel_.remove(highlight_);
        highlight_ = -1;
        dragSel_ = dragVert_ = -1;
        update();
        emit selectionsChanged();
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}
