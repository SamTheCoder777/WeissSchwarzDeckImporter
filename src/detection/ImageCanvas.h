#pragma once

#include <QWidget>
#include <QImage>
#include <QPolygonF>
#include <QVector>
#include <QString>

class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    enum Mode { Rectangle, Polygon, ClickOnly, Hand };
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setMode(Mode m);
    Mode mode() const { return mode_; }

    void clearSelections();
    void undo();

    int  selectionCount() const { return sel_.size(); }
    QPolygonF selection(int i) const { return sel_.value(i).poly; }

    void setHighlight(int index);
    int  highlight() const { return highlight_; }

    void setSelectionState(int index, bool confirmed, const QString& label);

    void addQuadSelection(const QPolygonF& quadImageCoords);

    int  selectionAtWidgetPoint(const QPointF& widgetPt) const;

    void reorder(const QVector<int>& newOrder);
    int selectionId(int i) const { return sel_.value(i).id; }

signals:
    void selectionsChanged();
    void selectionGeometryChanged(int i);
    void selectionClicked(int index);
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

    // cache
    QImage   scaledCache_;
    double   cachedScale_ = -1.0;
    QSize    cachedSize_;

    Mode mode_ = Rectangle;
    QVector<Sel> sel_;
    int  highlight_ = -1;

    // in-progress rectangle
    bool    dragging_ = false;
    QPointF dragStartImg_, dragCurImg_;

    // in-progress polygon
    QPolygonF polyInProgress_;
    QPointF   cursorImg_;
    bool      haveCursor_ = false;

    // vertex editing
    int dragSel_ = -1, dragVert_ = -1;
};
