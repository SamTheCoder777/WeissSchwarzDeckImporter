// ImageCanvas.h — image display + LabelMe-style card selection.
//
// Modes:
//   Rectangle : drag a box (snip style)
//   Polygon   : click corners, LIVE preview line follows the cursor,
//               Enter/double-click/right-click->Finish to close,
//               Ctrl+Z removes the last point, Esc cancels.
//
// Editing (both modes): drag any vertex of a committed selection to move it,
// click inside a selection to select it, right-click a selection to delete it.
//
// Selections are stored in IMAGE coordinates so they survive window resizing.
#pragma once

#include <QWidget>
#include <QImage>
#include <QPolygonF>
#include <QVector>
#include <QString>

class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    enum Mode { Rectangle, Polygon };
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setMode(Mode m);
    Mode mode() const { return mode_; }

    void clearSelections();
    void undo();                       // Ctrl+Z: last polygon point, else last selection

    int  selectionCount() const { return sel_.size(); }
    QPolygonF selection(int i) const { return sel_.value(i).poly; }

    void setHighlight(int index);
    int  highlight() const { return highlight_; }

    // confirmed state drives the colour + label drawn on the canvas
    void setSelectionState(int index, bool confirmed, const QString& label);

    // Add a detected card (4 corners in IMAGE coords) as a committed selection.
    void addQuadSelection(const QPolygonF& quadImageCoords);
    // Which committed selection contains this WIDGET-space point? -1 if none.
    int  selectionAtWidgetPoint(const QPointF& widgetPt) const;

    void reorder(const QVector<int>& newOrder);   // permute selections in place
    int selectionId(int i) const { return sel_.value(i).id; }

signals:
    void selectionsChanged();              // count changed (added/removed/cleared)
    void selectionGeometryChanged(int i);  // vertices moved -> crop must be recomputed
    void selectionClicked(int index);      // user clicked a selection on the image
    void canvasClickedImagePoint(const QPointF& imagePt);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    struct Sel {
        QPolygonF poly;
        bool      confirmed = false;
        QString   label;
        int       id = -1;
    };

    int nextSelId_ = 0;

    void    recomputeTransform();
    QPointF toImage(const QPointF& widgetPt) const;
    QPointF toWidget(const QPointF& imagePt) const;
    int     hitTestSelection(const QPointF& widgetPt) const;
    bool    hitTestVertex(const QPointF& widgetPt, int& selIdx, int& vertIdx) const;
    void    finishPolygon();

    QImage  image_;
    double  scale_ = 1.0;
    QPointF offset_{0, 0};

    Mode mode_ = Rectangle;
    QVector<Sel> sel_;
    int  highlight_ = -1;

    // in-progress rectangle
    bool    dragging_ = false;
    QPointF dragStartImg_, dragCurImg_;

    // in-progress polygon (+ live cursor for the preview line)
    QPolygonF polyInProgress_;
    QPointF   cursorImg_;
    bool      haveCursor_ = false;

    // vertex editing
    int dragSel_ = -1, dragVert_ = -1;
};
