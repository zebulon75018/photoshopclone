#pragma once
#include <QAbstractScrollArea>
#include <QPixmap>
#include <QTimer>
#include <QVector>
#include <QPolygonF>
#include "core/Document.h"
#include "tools/Tool.h"

// Zone de travail : affichage du composite avec zoom/panoramique, damier de transparence, fourmis marchantes,
// grille, et relais des événements souris/clavier vers l'outil actif (en coordonnées image).
class CanvasView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit CanvasView(Document* doc, QWidget* parent = nullptr);
    Document* document() const { return m_doc; }
    double zoom() const { return m_zoom; }

    QPointF toImage(const QPointF& w) const { return (w - m_origin) / m_zoom; }
    QPointF toWidget(const QPointF& i) const { return i * m_zoom + m_origin; }
    QRectF toWidget(const QRectF& r) const { return QRectF(toWidget(r.topLeft()), toWidget(r.bottomRight())); }
    QPointF viewCenterImage() const { return toImage(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0)); }

    void setZoom(double z, QPointF anchor = QPointF(-1, -1));
    void zoomIn(QPointF anchor = QPointF(-1, -1));
    void zoomOut(QPointF anchor = QPointF(-1, -1));
    void zoomFit(bool capAt100 = false);
    void zoom100();
    void zoomToRect(const QRectF& imageRect);
    void panBy(const QPointF& deltaWidget);
    void refresh() { viewport()->update(); }
    void updateToolCursor();

    bool showGrid = false;
    bool showEdges = true;
    bool suppressAnts = false;         // un outil dessine lui-même le contour de sélection (ex. transformation de la sélection)

signals:
    void zoomChanged(double);
    void cursorMoved(QPoint imagePos, bool inside);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void leaveEvent(QEvent*) override;
    void scrollContentsBy(int dx, int dy) override;
    void focusOutEvent(QFocusEvent*) override;

private:
    Tool* activeTool() const;
    ToolEvent makeEvent(QMouseEvent* e) const;
    void clampOrigin();
    void updateScrollbars();
    void rebuildAnts();
    void onDocChanged(const QRect& r);

    Document* m_doc;
    double m_zoom = 1.0;
    QPointF m_origin{0, 0};
    QPixmap m_checker;
    bool m_space = false, m_panning = false, m_fitted = true, m_updatingBars = false;
    QPointF m_panLast;
    Tool* m_dragTool = nullptr;
    QVector<QPolygonF> m_ants;
    bool m_antsDirty = true;
    qreal m_antOffset = 0;
    QTimer m_antTimer;
};
