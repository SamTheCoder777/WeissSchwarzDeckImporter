#include "ImageCanvas.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <algorithm>
#include <cmath>

static constexpr double VERTEX_HIT_PX = 9.0;

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);                 // needed for the live polygon preview
    setMinimumSize(400, 300);
}

void ImageCanvas::reorder(const QVector<int>& order) {
    if (order.size() != sel_.size()) return;
    QVector<Sel> reordered;
    reordered.reserve(sel_.size());
    for (int idx : order) reordered.push_back(sel_[idx]);
    sel_ = reordered;
    highlight_ = -1;            // indices changed; clear stale highlight
    update();
}

void ImageCanvas::setImage(const QImage& img) {
    image_ = img;
    scaledCache_ = QImage();
    cachedScale_ = -1.0;
    sel_.clear();
    polyInProgress_.clear();
    highlight_ = -1;
    haveCursor_ = false;
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
    highlight_ = -1;
    update();
    emit selectionsChanged();
}

void ImageCanvas::undo() {
    if (!polyInProgress_.isEmpty()) {       // remove the last placed point
        polyInProgress_.removeLast();
        update();
        return;
    }
    if (!sel_.isEmpty()) {                  // otherwise drop the last selection
        sel_.removeLast();
        if (highlight_ >= sel_.size()) highlight_ = -1;
        update();
        emit selectionsChanged();
    }
}

void ImageCanvas::setHighlight(int index) { highlight_ = index; update(); }

void ImageCanvas::addQuadSelection(const QPolygonF& quad) {
    if (quad.size() < 3) return;
    Sel s;
    s.poly = quad;
    s.id = nextSelId_++;
    sel_.push_back(s);
    update();
    emit selectionsChanged();
}

int ImageCanvas::selectionAtWidgetPoint(const QPointF& widgetPt) const {
    return hitTestSelection(widgetPt);   // existing private helper (image-space test inside)
}

void ImageCanvas::setSelectionState(int index, bool confirmed, const QString& label) {
    if (index < 0 || index >= sel_.size()) return;
    sel_[index].confirmed = confirmed;
    sel_[index].label     = label;
    update();
}

void ImageCanvas::recomputeTransform() {
    if (image_.isNull()) { scale_ = 1.0; offset_ = {0, 0}; return; }
    double sx = (double)width() / image_.width();
    double sy = (double)height() / image_.height();
    scale_ = std::min(sx, sy);
    offset_ = QPointF((width()  - image_.width()  * scale_) / 2.0,
                      (height() - image_.height() * scale_) / 2.0);
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

    const int tw = int(image_.width()  * scale_);
    const int th = int(image_.height() * scale_);

    // rebuild the scaled cache only when the scale/target size changed
    if (scaledCache_.isNull() || cachedScale_ != scale_ || cachedSize_ != QSize(tw, th)) {
        scaledCache_ = image_.scaled(tw, th, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        cachedScale_ = scale_;
        cachedSize_  = QSize(tw, th);
    }
    g.drawImage(QPointF(offset_.x(), offset_.y()), scaledCache_);

    for (int i = 0; i < sel_.size(); ++i) {
        QPolygonF wp;
        for (const QPointF& ip : sel_[i].poly) wp << toWidget(ip);

        const bool hi = (i == highlight_);
        //  confirmed = green, unconfirmed = cyan, highlighted = yellow
        QColor base = sel_[i].confirmed ? QColor(70, 220, 120) : QColor(40, 190, 255);
        QColor col  = hi ? QColor(255, 205, 40) : base;

        g.setPen(QPen(col, hi ? 3 : 2));
        g.setBrush(QColor(col.red(), col.green(), col.blue(), hi ? 55 : 28));
        g.drawPolygon(wp);

        g.setBrush(col);
        for (const QPointF& p : wp) g.drawEllipse(p, 3.5, 3.5);

        // label: "1" when unconfirmed, "1  BD/W125-021 x2" when confirmed
        // if (!wp.isEmpty()) {
        //     QString txt = QString::number(i + 1);
        //     if (!sel_[i].label.isEmpty()) txt += "  " + sel_[i].label;
        //     QPointF anchor = wp.boundingRect().topLeft() + QPointF(4, 4);
        //     QFontMetrics fm(g.font());
        //     QRectF box(anchor, QSizeF(fm.horizontalAdvance(txt) + 8, fm.height() + 4));
        //     g.setPen(Qt::NoPen);
        //     g.setBrush(QColor(0, 0, 0, 165));
        //     g.drawRect(box);
        //     g.setPen(sel_[i].confirmed ? QColor(150, 255, 190) : Qt::white);
        //     g.drawText(box.adjusted(4, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, txt);
        // }
    }

    // in-progress rectangle
    if (dragging_ && mode_ == Rectangle) {
        QRectF r(toWidget(dragStartImg_), toWidget(dragCurImg_));
        g.setPen(QPen(Qt::white, 1, Qt::DashLine));
        g.setBrush(QColor(255, 255, 255, 25));
        g.drawRect(r.normalized());
    }

    // in-progress polygon
    if (!polyInProgress_.isEmpty()) {
        QPolygonF wp;
        for (const QPointF& ip : polyInProgress_) wp << toWidget(ip);
        g.setPen(QPen(QColor(255, 255, 255), 2));
        g.setBrush(Qt::NoBrush);
        g.drawPolyline(wp);

        if (haveCursor_) {
            QPointF cur = toWidget(cursorImg_);
            g.setPen(QPen(QColor(255, 255, 255, 170), 1, Qt::DashLine));
            g.drawLine(wp.back(), cur);                       // rubber band
            if (wp.size() >= 2) g.drawLine(cur, wp.front());  // closing preview
            // shaded preview of the resulting polygon
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
            g.drawEllipse(wp[i], i == 0 ? 5.0 : 3.5, i == 0 ? 5.0 : 3.5);  // first point bigger
    }
}

void ImageCanvas::mousePressEvent(QMouseEvent* e) {
    if (image_.isNull()) return;
    const QPointF ip = toImage(e->position());

    if (e->button() == Qt::LeftButton) {
        // Auto-detect "click to pick a card": report the point and do nothing else.
        if (mode_ == ClickOnly) {
            emit canvasClickedImagePoint(ip);
            return;                       // no vertex-drag, no selection, no rectangle
        }

        emit canvasClickedImagePoint(ip);

        int si, vi;
        if (polyInProgress_.isEmpty() && hitTestVertex(e->position(), si, vi)) {
            dragSel_ = si; dragVert_ = vi;
            return;
        }

        if (polyInProgress_.isEmpty()) {
            int hit = hitTestSelection(e->position());
            if (hit >= 0) {
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
    cursorImg_ = toImage(e->position());
    haveCursor_ = true;

    if (dragSel_ >= 0 && dragVert_ >= 0 && mode_ == Hand) { // moving a vertex
        sel_[dragSel_].poly[dragVert_] = cursorImg_;
        update();
        return;
    }
    if (dragging_ && mode_ == Rectangle)
        dragCurImg_ = cursorImg_;
    if (!polyInProgress_.isEmpty() || dragging_) update();   // live preview
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (dragSel_ >= 0) {                              // finished moving a vertex
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

// right click
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
            sel_[target].poly.remove(vi);
            emit selectionGeometryChanged(target);
            update();
        } else if (a == delSel) {
            sel_.remove(target);
            if (highlight_ >= sel_.size()) highlight_ = -1;
            update();
            emit selectionsChanged();
        }
    }
}

void ImageCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) { undo(); return; }        // Ctrl+Z
    switch (e->key()) {
    case Qt::Key_Return: case Qt::Key_Enter:
        if (mode_ == Polygon) finishPolygon();
        break;
    case Qt::Key_Escape:
        polyInProgress_.clear(); update();
        break;
    case Qt::Key_Backspace: case Qt::Key_Delete:
        undo();
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}
