#include "CanvasView.h"
#include "core/Workspace.h"
#include "tools/ToolManager.h"
#include "core/Selection.h"
#include "core/MatUtil.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <cmath>

static const double kMargin = 40;

CanvasView::CanvasView(Document* doc, QWidget* parent) : QAbstractScrollArea(parent), m_doc(doc) {
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    m_checker = QPixmap(16, 16);
    { QPainter p(&m_checker); p.fillRect(0, 0, 16, 16, QColor(255, 255, 255));
      p.fillRect(0, 0, 8, 8, QColor(204, 204, 204)); p.fillRect(8, 8, 8, 8, QColor(204, 204, 204)); }
    connect(doc, &Document::changed, this, &CanvasView::onDocChanged);
    connect(doc, &Document::selectionChanged, this, [this] { m_antsDirty = true; viewport()->update(); });
    connect(doc, &Document::sizeChanged, this, [this] { zoomFit(true); });
    m_antTimer.setInterval(130);
    connect(&m_antTimer, &QTimer::timeout, this, [this] {
        m_antOffset += 1;
        if (m_doc->hasSelection() && showEdges && !suppressAnts) viewport()->update();
    });
    m_antTimer.start();
    connect(&Workspace::instance(), &Workspace::modalChanged, this, [this] { updateToolCursor(); });
}

// ------------------------------------------------------------------------------------------ zoom / pan
static const double kSteps[] = {1 / 32.0, 1 / 16.0, 1 / 12.0, 1 / 8.0, 1 / 6.0, 1 / 4.0, 1 / 3.0, 1 / 2.0, 2 / 3.0, 1, 2, 3, 4, 5, 6, 8, 12, 16, 32, 64};

void CanvasView::setZoom(double z, QPointF anchor) {
    z = std::clamp(z, 1 / 64.0, 64.0);
    if (anchor.x() < 0) anchor = QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0);
    QPointF img = toImage(anchor);
    m_zoom = z;
    m_origin = anchor - img * z;
    m_fitted = false;
    clampOrigin(); updateScrollbars(); viewport()->update();
    emit zoomChanged(m_zoom);
}

void CanvasView::zoomIn(QPointF a) { for (double s : kSteps) if (s > m_zoom * 1.001) { setZoom(s, a); return; } }
void CanvasView::zoomOut(QPointF a) { for (int i = int(std::size(kSteps)) - 1; i >= 0; --i) if (kSteps[i] < m_zoom * 0.999) { setZoom(kSteps[i], a); return; } }
void CanvasView::zoom100() { setZoom(1.0); }

void CanvasView::zoomFit(bool cap) {
    QSize vs = viewport()->size(), is = m_doc->size();
    if (vs.isEmpty()) { m_fitted = true; return; }
    double z = std::min((vs.width() - 2 * kMargin) / is.width(), (vs.height() - 2 * kMargin) / is.height());
    if (cap) z = std::min(z, 1.0);
    setZoom(std::max(z, 1 / 64.0));
    m_fitted = true;
}

void CanvasView::zoomToRect(const QRectF& r) {
    if (r.width() < 1 || r.height() < 1) return;
    QSize vs = viewport()->size();
    double z = std::clamp(std::min(vs.width() / r.width(), vs.height() / r.height()), 1 / 64.0, 64.0);
    m_zoom = z; m_fitted = false;
    m_origin = QPointF(vs.width() / 2.0, vs.height() / 2.0) - r.center() * z;
    clampOrigin(); updateScrollbars(); viewport()->update();
    emit zoomChanged(m_zoom);
}

void CanvasView::panBy(const QPointF& d) {
    m_origin += d;
    clampOrigin(); updateScrollbars(); viewport()->update();
}

void CanvasView::clampOrigin() {
    QSize vs = viewport()->size();
    double iw = m_doc->size().width() * m_zoom, ih = m_doc->size().height() * m_zoom;
    m_origin.setX(iw <= vs.width() ? (vs.width() - iw) / 2 : std::clamp(m_origin.x(), vs.width() - iw - kMargin, kMargin));
    m_origin.setY(ih <= vs.height() ? (vs.height() - ih) / 2 : std::clamp(m_origin.y(), vs.height() - ih - kMargin, kMargin));
    m_origin = QPointF(std::round(m_origin.x()), std::round(m_origin.y()));   // alignement pixel : pas de bavure du damier
}

void CanvasView::updateScrollbars() {
    m_updatingBars = true;
    QSize vs = viewport()->size();
    double iw = m_doc->size().width() * m_zoom, ih = m_doc->size().height() * m_zoom;
    auto cfg = [&](QScrollBar* b, double content, int view, double origin) {
        if (content <= view) { b->setRange(0, 0); return; }
        b->setRange(0, int(content + 2 * kMargin - view));
        b->setPageStep(view); b->setSingleStep(24);
        b->setValue(int(kMargin - origin));
    };
    cfg(horizontalScrollBar(), iw, vs.width(), m_origin.x());
    cfg(verticalScrollBar(), ih, vs.height(), m_origin.y());
    m_updatingBars = false;
}

void CanvasView::scrollContentsBy(int, int) {
    if (m_updatingBars) return;
    QSize vs = viewport()->size();
    if (m_doc->size().width() * m_zoom > vs.width()) m_origin.setX(kMargin - horizontalScrollBar()->value());
    if (m_doc->size().height() * m_zoom > vs.height()) m_origin.setY(kMargin - verticalScrollBar()->value());
    viewport()->update();
}

void CanvasView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    if (m_fitted) zoomFit(true);
    else { clampOrigin(); updateScrollbars(); }
}

void CanvasView::onDocChanged(const QRect& r) {
    viewport()->update(toWidget(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

// ------------------------------------------------------------------------------------------ dessin
void CanvasView::rebuildAnts() {
    m_ants.clear();
    for (auto& c : Sel::contours(m_doc->selection())) {
        QPolygonF p;
        for (auto& pt : c) p << QPointF(pt.x + 0.5, pt.y + 0.5);
        if (p.size() >= 2) m_ants << p;
    }
    m_antsDirty = false;
}

void CanvasView::paintEvent(QPaintEvent* ev) {
    QPainter p(viewport());
    p.fillRect(ev->rect(), QColor(0x3a, 0x3a, 0x3a));
    const QSize is = m_doc->size();
    QRectF ir(m_origin, QSizeF(is.width() * m_zoom, is.height() * m_zoom));
    QRect vis = ir.toAlignedRect() & viewport()->rect();
    if (!vis.isEmpty()) {
        p.fillRect(ir.translated(2, 3).toAlignedRect() & viewport()->rect().adjusted(0, 0, 3, 3), QColor(0, 0, 0, 70));   // ombre portée
        QPoint off((vis.left() - int(std::floor(ir.left()))) % 16, (vis.top() - int(std::floor(ir.top()))) % 16);
        p.drawTiledPixmap(vis, m_checker, off);
        // image (mapping pixel-exact : rectangle source entier -> rectangle cible)
        QRectF srcF = QRectF(toImage(vis.topLeft()), toImage(QPointF(vis.right() + 1, vis.bottom() + 1)));
        QRect src(QPoint(int(std::floor(srcF.left())), int(std::floor(srcF.top()))), QPoint(int(std::ceil(srcF.right())), int(std::ceil(srcF.bottom()))));
        src &= m_doc->rect();
        if (!src.isEmpty()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
            p.setClipRect(vis);
            p.drawImage(toWidget(QRectF(src)), m_doc->display(), QRectF(src));
            p.setClipping(false);
        }
        if (m_zoom >= 8.0) {   // grille de pixels
            p.setPen(QPen(QColor(128, 128, 128, 90), 1));
            for (int x = std::max(0, int(std::floor(toImage(vis.topLeft()).x()))); x <= std::min(is.width(), int(std::ceil(toImage(vis.bottomRight()).x()))); ++x)
                p.drawLine(QPointF(toWidget(QPointF(x, 0)).x(), vis.top()), QPointF(toWidget(QPointF(x, 0)).x(), vis.bottom()));
            for (int y = std::max(0, int(std::floor(toImage(vis.topLeft()).y()))); y <= std::min(is.height(), int(std::ceil(toImage(vis.bottomRight()).y()))); ++y)
                p.drawLine(QPointF(vis.left(), toWidget(QPointF(0, y)).y()), QPointF(vis.right(), toWidget(QPointF(0, y)).y()));
        }
        if (showGrid) {
            p.setPen(QPen(QColor(0, 160, 255, 110), 1));
            const int step = 50;
            for (int x = 0; x <= is.width(); x += step) p.drawLine(QPointF(toWidget(QPointF(x, 0)).x(), vis.top()), QPointF(toWidget(QPointF(x, 0)).x(), vis.bottom()));
            for (int y = 0; y <= is.height(); y += step) p.drawLine(QPointF(vis.left(), toWidget(QPointF(0, y)).y()), QPointF(vis.right(), toWidget(QPointF(0, y)).y()));
        }
    }
    // fourmis marchantes
    if (m_doc->hasSelection() && showEdges && !suppressAnts) {
        if (m_antsDirty) rebuildAnts();
        p.save();
        p.translate(m_origin); p.scale(m_zoom, m_zoom);
        p.setBrush(Qt::NoBrush);
        QPen white(Qt::white, 0); QPen black(Qt::black, 0);
        black.setDashPattern({4, 4}); black.setDashOffset(m_antOffset);
        for (const QPolygonF& poly : m_ants) { p.setPen(white); p.drawPolygon(poly); p.setPen(black); p.drawPolygon(poly); }
        p.restore();
    }
    if (Tool* t = Workspace::instance().tools()->current()) {
        p.setRenderHint(QPainter::Antialiasing, false);
        t->paintOverlay(p, this);
    }
}

// ------------------------------------------------------------------------------------------ événements
Tool* CanvasView::activeTool() const {
    auto* tm = Workspace::instance().tools();
    if (Workspace::instance().modalLocked()) return tm->hand();   // dialogue ouvert : seulement se déplacer dans l'image
    if (m_space) return (QApplication::keyboardModifiers() & Qt::ControlModifier) ? tm->zoomTool() : tm->hand();
    return tm->current();
}

ToolEvent CanvasView::makeEvent(QMouseEvent* e) const {
    ToolEvent ev;
    ev.widgetPos = e->localPos(); ev.pos = toImage(ev.widgetPos);
    ev.mods = e->modifiers(); ev.button = e->button(); ev.buttons = e->buttons();
    ev.view = const_cast<CanvasView*>(this); ev.doc = m_doc;
    return ev;
}

void CanvasView::updateToolCursor() {
    Tool* t = activeTool();
    viewport()->setCursor(m_panning ? QCursor(Qt::ClosedHandCursor) : (t ? t->cursor() : QCursor(Qt::ArrowCursor)));
}

void CanvasView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (e->button() == Qt::MiddleButton) { m_panning = true; m_panLast = e->localPos(); updateToolCursor(); return; }
    m_dragTool = activeTool();
    if (m_dragTool) m_dragTool->press(makeEvent(e));
    updateToolCursor();
}

void CanvasView::mouseMoveEvent(QMouseEvent* e) {
    QPointF ip = toImage(e->localPos());
    emit cursorMoved(QPoint(int(std::floor(ip.x())), int(std::floor(ip.y()))), m_doc->rect().contains(int(std::floor(ip.x())), int(std::floor(ip.y()))));
    if (m_panning) { panBy(e->localPos() - m_panLast); m_panLast = e->localPos(); return; }
    Tool* t = (e->buttons() && m_dragTool) ? m_dragTool : activeTool();
    if (t) t->move(makeEvent(e));
}

void CanvasView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton && m_panning) { m_panning = false; updateToolCursor(); return; }
    if (m_dragTool) { m_dragTool->release(makeEvent(e)); m_dragTool = nullptr; }
    updateToolCursor();
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (Tool* t = activeTool()) t->doubleClick(makeEvent(e));
}

void CanvasView::wheelEvent(QWheelEvent* e) {
    QPoint ad = e->angleDelta();
    if (e->modifiers() & Qt::AltModifier) {   // Alt + molette : zoom centré sur le curseur (comme Photoshop)
        int d = ad.y() ? ad.y() : ad.x();
        #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        setZoom(m_zoom * std::pow(1.2, d / 120.0), e->position());
#else
        setZoom(m_zoom * std::pow(1.2, d / 120.0), e->posF());
#endif
    } else if (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        panBy(QPointF((ad.y() ? ad.y() : ad.x()) * 0.6, 0));
    } else {
        panBy(QPointF(ad.x() * 0.6, ad.y() * 0.6));
    }
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* e) {
    if (Workspace::instance().modalLocked()) { e->ignore(); return; }
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { m_space = true; updateToolCursor(); return; }
    if (Tool* t = Workspace::instance().tools()->current())
        if (t->keyPress(e, this)) { e->accept(); return; }
    e->ignore();
}

void CanvasView::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { m_space = false; updateToolCursor(); return; }
    e->ignore();
}

void CanvasView::focusOutEvent(QFocusEvent* e) { m_space = false; QAbstractScrollArea::focusOutEvent(e); }
void CanvasView::leaveEvent(QEvent* e) { emit cursorMoved(QPoint(), false); QAbstractScrollArea::leaveEvent(e); }
