#include "Tools.h"
#include "ToolManager.h"
#include "core/MatUtil.h"
#include "ui/CanvasView.h"
#include "ui/OptionsBar.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

static cv::Mat translation(double dx, double dy) { return (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy); }

// ============================================================================ MoveTool
void MoveTool::buildOptions(OptionsBar& o) {
    o.addLabel("Ctrl+clic : sélectionner le calque sous le curseur   •   Maj : contraindre l'axe   •   Flèches : décaler de 1 px (Maj = 10 px)");
}

void MoveTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    if (e.ctrl()) {   // sélection automatique du calque
        const auto& L = e.doc->layers();
        cv::Point p(int(e.pos.x()), int(e.pos.y()));
        for (int i = int(L.size()) - 1; i >= 0; --i)
            if (L[i]->props.visible && mu::bounds(L[i]->image).contains(p) && L[i]->image.at<cv::Vec4b>(p)[3] > 0) { e.doc->setActiveIndex(i); break; }
    }
    if (!m_fc.begin(e.doc)) return;
    m_start = e.pos; m_drag = true;
}

void MoveTool::move(const ToolEvent& e) {
    if (!m_drag) return;
    double dx = std::round(e.pos.x() - m_start.x()), dy = std::round(e.pos.y() - m_start.y());
    if (e.shift()) { if (std::fabs(dx) > std::fabs(dy)) dy = 0; else dx = 0; }
    m_fc.update(translation(dx, dy));
}

void MoveTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    double dx = std::round(e.pos.x() - m_start.x()), dy = std::round(e.pos.y() - m_start.y());
    if (dx == 0 && dy == 0) m_fc.cancel(); else m_fc.commit("Déplacer");
}

bool MoveTool::keyPress(QKeyEvent* k, CanvasView* v) {
    int step = (k->modifiers() & Qt::ShiftModifier) ? 10 : 1, dx = 0, dy = 0;
    switch (k->key()) {
    case Qt::Key_Left: dx = -step; break; case Qt::Key_Right: dx = step; break;
    case Qt::Key_Up: dy = -step; break; case Qt::Key_Down: dy = step; break;
    default: return false;
    }
    if (m_drag) return true;
    if (m_fc.begin(v->document())) { m_fc.update(translation(dx, dy)); m_fc.commit("Décaler"); }
    return true;
}

// ============================================================================ TransformTool (pixels)
bool TransformTool::begin(Document* d) {
    if (!m_fc.begin(d)) return false;
    m_doc = d;
    m_box.reset(m_fc.sourceRect());
    m_dirty = false;
    return true;
}

void TransformTool::activate() {
    Workspace::instance().message("Transformation : Entrée = valider, Échap = annuler, Maj = proportionnel / angle 15°, Alt = depuis le centre");
}

void TransformTool::press(const ToolEvent& e) { if (e.button == Qt::LeftButton && m_fc.active()) m_box.press(e); }

void TransformTool::move(const ToolEvent& e) {
    if (!m_fc.active()) return;
    if (m_box.move(e)) { apply(); e.view->refresh(); }
}

void TransformTool::apply() {
    m_fc.update(m_box.matrix());
    m_dirty = true;
}

bool TransformTool::keyPress(QKeyEvent* k, CanvasView*) {
    if (!m_fc.active()) return false;
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { commit(); return true; }
    if (k->key() == Qt::Key_Escape) { cancel(); emit finished(); return true; }
    return false;
}

void TransformTool::commit() {
    if (!m_fc.active()) return;
    if (m_dirty) m_fc.commit("Transformation manuelle"); else m_fc.cancel();
    m_dirty = false;
    emit finished();
}

void TransformTool::deactivate() {   // changer d'outil valide la transformation en cours (comme Photoshop)
    if (!m_fc.active()) return;
    if (m_dirty) m_fc.commit("Transformation manuelle"); else m_fc.cancel();
    m_dirty = false;
}

void TransformTool::cancel() {
    if (m_fc.active()) m_fc.cancel();
    m_dirty = false;
}

void TransformTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (m_fc.active()) m_box.paint(p, v);
}
