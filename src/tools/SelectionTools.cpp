#include "Tools.h"
#include "core/MatUtil.h"
#include "core/Operations.h"
#include "ui/CanvasView.h"
#include "ui/OptionsBar.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

// Combinaison d'une nouvelle forme avec la sélection existante.
static void commitShape(Document* d, const cv::Mat& shape, Sel::Mode mode, const QString& name) {
    cv::Mat s = shape;
    int f = Workspace::instance().settings.feather;
    if (f > 0) s = Sel::feather(s, f);
    d->setSelection(Sel::combine(d->selection(), s, mode, d->size()), name);
}

static Sel::Mode modeFor(const ToolEvent& e) {
    if (e.shift() && e.alt()) return Sel::Mode::Intersect;
    if (e.shift()) return Sel::Mode::Add;
    if (e.alt()) return Sel::Mode::Subtract;
    return Workspace::instance().settings.selMode;
}

static void drawAnts(QPainter& p, const std::function<void()>& draw) {
    p.setBrush(Qt::NoBrush);
    QPen w(Qt::white, 1), b(Qt::black, 1, Qt::DashLine);
    p.setPen(w); draw();
    p.setPen(b); draw();
}

// ============================================================================ Marquee
void MarqueeTool::buildOptions(OptionsBar& o) {
    o.addSelectionModes(&st().selMode);
    o.addSeparator();
    o.addSpin(tr("Contour progressif :"), &st().feather, 0, 250, " px");
    o.addCheck(tr("Lissage"), &st().antiAlias);
    o.addLabel(tr("   Maj : carré/cercle • Alt : depuis le centre"));
}

QRectF MarqueeTool::rectFor(const ToolEvent& e) const {
    QPointF a = m_start, b = e.pos;
    double dx = b.x() - a.x(), dy = b.y() - a.y();
    if (e.shift()) { double s = std::max(std::fabs(dx), std::fabs(dy)); dx = std::copysign(s, dx); dy = std::copysign(s, dy); }
    if (e.alt()) return QRectF(a.x() - std::fabs(dx), a.y() - std::fabs(dy), 2 * std::fabs(dx), 2 * std::fabs(dy));
    return QRectF(a, QPointF(a.x() + dx, a.y() + dy)).normalized();
}

void MarqueeTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    m_drag = true; m_start = e.pos; m_rect = QRectF(e.pos, QSizeF(0, 0)); m_mode = modeFor(e);
}

void MarqueeTool::move(const ToolEvent& e) {
    if (!m_drag) return;
    m_rect = rectFor(e);
    e.view->refresh();
    Workspace::instance().message(tr("Sélection : %1 × %2 px").arg(int(m_rect.width())).arg(int(m_rect.height())));
}

void MarqueeTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    QRectF r = rectFor(e);
    e.view->refresh();
    if (r.width() < 2 || r.height() < 2) { if (m_mode == Sel::Mode::Replace) Ops::deselect(e.doc); return; }
    r = QRectF(std::round(r.left()), std::round(r.top()), std::round(r.width()), std::round(r.height()));
    cv::Mat shape = m_ellipse ? Sel::fromEllipse(e.doc->size(), r) : Sel::fromRect(e.doc->size(), r);
    if (!st().antiAlias && m_ellipse) cv::threshold(shape, shape, 127, 255, cv::THRESH_BINARY);
    commitShape(e.doc, shape, m_mode, name().section(" (", 0, 0));
}

void MarqueeTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag) return;
    QRectF r = v->toWidget(m_rect);
    drawAnts(p, [&] { m_ellipse ? p.drawEllipse(r) : p.drawRect(r); });
}

// ============================================================================ Lasso
void LassoTool::buildOptions(OptionsBar& o) {
    o.addSelectionModes(&st().selMode);
    o.addSeparator();
    o.addSpin(tr("Contour progressif :"), &st().feather, 0, 250, " px");
}
void LassoTool::press(const ToolEvent& e) { if (e.button == Qt::LeftButton) { m_drag = true; m_poly.clear(); m_poly << e.pos; m_mode = modeFor(e); } }
void LassoTool::move(const ToolEvent& e) {
    if (!m_drag) return;
    if (QLineF(m_poly.last(), e.pos).length() >= 1.0 / std::max(0.25, e.view->zoom())) { m_poly << e.pos; e.view->refresh(); }
}
void LassoTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    if (m_poly.size() >= 3) commitShape(e.doc, Sel::fromPolygon(e.doc->size(), m_poly), m_mode, tr("Lasso"));
    m_poly.clear();
    e.view->refresh();
}
void LassoTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag || m_poly.size() < 2) return;
    QPolygonF w; for (auto& pt : m_poly) w << v->toWidget(pt);
    drawAnts(p, [&] { p.drawPolyline(w); });
}

// ============================================================================ Lasso polygonal
void PolyLassoTool::buildOptions(OptionsBar& o) {
    o.addSelectionModes(&st().selMode);
    o.addSeparator();
    o.addSpin(tr("Contour progressif :"), &st().feather, 0, 250, " px");
    o.addLabel(tr("   Double-clic / Entrée : fermer • Retour arrière : annuler le dernier point • Maj : angles de 45°"));
}
void PolyLassoTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    QPointF p = e.pos;
    if (m_poly.isEmpty()) m_mode = modeFor(e);
    else {
        if (e.shift()) {   // contrainte 45°
            QPointF l = m_poly.last(), d = p - l; double a = std::round(std::atan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4), len = std::hypot(d.x(), d.y());
            p = l + QPointF(std::cos(a) * len, std::sin(a) * len);
        }
        if (m_poly.size() >= 3 && QLineF(e.view->toWidget(m_poly.first()), e.widgetPos).length() < 8) { close(); return; }
    }
    m_poly << p;
    e.view->refresh();
}
void PolyLassoTool::move(const ToolEvent& e) { m_hover = e.pos; if (!m_poly.isEmpty()) e.view->refresh(); }
void PolyLassoTool::close() {
    Document* d = Workspace::instance().doc();
    if (d && m_poly.size() >= 3) commitShape(d, Sel::fromPolygon(d->size(), m_poly), m_mode, tr("Lasso polygonal"));
    m_poly.clear();
    if (auto* v = Workspace::instance().view()) v->refresh();
}
bool PolyLassoTool::keyPress(QKeyEvent* k, CanvasView* v) {
    if (m_poly.isEmpty()) return false;
    switch (k->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: close(); return true;
    case Qt::Key_Escape: m_poly.clear(); v->refresh(); return true;
    case Qt::Key_Backspace: case Qt::Key_Delete: m_poly.removeLast(); v->refresh(); return true;
    }
    return false;
}
void PolyLassoTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (m_poly.isEmpty()) return;
    QPolygonF w; for (auto& pt : m_poly) w << v->toWidget(pt);
    w << v->toWidget(m_hover);
    drawAnts(p, [&] { p.drawPolyline(w); });
    p.setPen(Qt::black); p.setBrush(Qt::white); p.drawRect(QRectF(w.first() - QPointF(3, 3), QSizeF(6, 6)));
}

// ============================================================================ Baguette magique
void WandTool::buildOptions(OptionsBar& o) {
    o.addSelectionModes(&st().selMode);
    o.addSeparator();
    o.addSpin(tr("Tolérance :"), &st().tolerance, 0, 255);
    o.addCheck(tr("Lissage"), &st().antiAlias);
    o.addCheck(tr("Pixels contigus"), &st().contiguous);
    o.addCheck(tr("Échantillonner tous les calques"), &st().sampleAll);
}
void WandTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    auto l = e.doc->activeLayer();
    if (!l && !st().sampleAll) return;
    const cv::Mat& src = st().sampleAll ? e.doc->composite() : l->image;
    cv::Mat m = Sel::magicWand(src, cv::Point(int(std::floor(e.pos.x())), int(std::floor(e.pos.y()))), st().tolerance, st().contiguous, st().antiAlias);
    if (m.empty()) return;
    commitShape(e.doc, m, modeFor(e), tr("Baguette magique"));
}
