#include "Tools.h"
#include "core/MatUtil.h"
#include "core/Operations.h"
#include "ui/CanvasView.h"
#include "ui/Dialogs.h"
#include "ui/OptionsBar.h"
#include <QImage>
#include <QPainter>
#include <cmath>

// ============================================================================ Texte
void TextTool::buildOptions(OptionsBar& o) {
    o.addFontControls();
    o.addLabel(tr("  Clic : nouveau texte / modifier le calque texte actif"));
}

void TextTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    auto l = e.doc->activeLayer();
    TextData td;
    Layer::Ptr existing;
    if (l && l->isText() && l->textBounds().contains(e.pos)) { existing = l; td = *l->text; }
    else {
        td.family = st().fontFamily; td.pixelSize = st().fontSize; td.bold = st().bold; td.italic = st().italic;
        td.align = st().textAlign; td.color = ws().fg; td.pos = e.pos;
    }
    if (!TextDialog::edit(e.view, td) || td.text.trimmed().isEmpty()) return;
    Ops::commitText(e.doc, existing, td);
}

// ============================================================================ Formes
void ShapeTool::buildOptions(OptionsBar& o) {
    o.addCombo(tr("Forme :"), {tr("Rectangle"), tr("Rectangle arrondi"), tr("Ellipse"), tr("Ligne")}, &st().shapeKind);
    o.addCheck(tr("Remplissage (couleur PP)"), &st().shapeFill);
    o.addCheck(tr("Contour (couleur AP)"), &st().shapeStroke);
    o.addSpin(tr("Épaisseur :"), &st().strokeWidth, 1, 200, " px");
    o.addSpin(tr("Rayon :"), &st().cornerRadius, 0, 500, " px");
    o.addSpin(tr("Opacité :"), &st().opacity, 1, 100, " %");
}

QRectF ShapeTool::rectFor(const ToolEvent& e) const {
    QPointF a = m_a, b = e.pos;
    double dx = b.x() - a.x(), dy = b.y() - a.y();
    if (e.shift() && st().shapeKind != 3) { double s = std::max(std::fabs(dx), std::fabs(dy)); dx = std::copysign(s, dx); dy = std::copysign(s, dy); }
    if (e.alt() && st().shapeKind != 3) return QRectF(a.x() - std::fabs(dx), a.y() - std::fabs(dy), 2 * std::fabs(dx), 2 * std::fabs(dy));
    return QRectF(a, QPointF(a.x() + dx, a.y() + dy)).normalized();
}

void ShapeTool::move(const ToolEvent& e) {
    if (!m_drag) return;
    m_b = e.pos; m_rect = rectFor(e); e.view->refresh();
}

void ShapeTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag) return;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 3));
    auto draw = [&] {
        if (st().shapeKind == 3) p.drawLine(v->toWidget(m_a), v->toWidget(m_b));
        else if (st().shapeKind == 2) p.drawEllipse(v->toWidget(m_rect));
        else p.drawRect(v->toWidget(m_rect));
    };
    draw(); p.setPen(QPen(Qt::white, 1)); draw();
}

void ShapeTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    QRectF r = rectFor(e);
    e.view->refresh();
    bool line = st().shapeKind == 3;
    if (!line && (r.width() < 2 || r.height() < 2)) return;
    if (line && QLineF(m_a, e.pos).length() < 2) return;
    QImage img(e.doc->size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen = st().shapeStroke ? QPen(ws().bg, st().strokeWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin) : QPen(Qt::NoPen);
        p.setPen(pen);
        p.setBrush(st().shapeFill ? QBrush(ws().fg) : QBrush(Qt::NoBrush));
        switch (st().shapeKind) {
        case 0: p.drawRect(r); break;
        case 1: p.drawRoundedRect(r, st().cornerRadius, st().cornerRadius); break;
        case 2: p.drawEllipse(r); break;
        default: p.setPen(QPen(ws().fg, st().strokeWidth, Qt::SolidLine, Qt::RoundCap)); p.drawLine(m_a, e.pos); break;
        }
    }
    cv::Mat m = mu::fromQImage(img);
    if (e.doc->hasSelection() || st().opacity < 100) {
        cv::Mat full = mu::newMat(e.doc->size());
        Blend::over(full, m, {0, 0}, mu::bounds(full), BlendMode::Normal, float(st().opacity / 100.0), cv::Mat(), e.doc->selection());
        m = full;
    }
    Ops::addLayerWithImage(e.doc, m, tr("Forme"));
}

// ============================================================================ Main / Zoom
void HandTool::move(const ToolEvent& e) {
    if (!m_drag) return;
    e.view->panBy(e.widgetPos - m_last);
    m_last = e.widgetPos;
}

void ZoomTool::press(const ToolEvent& e) { m_drag = true; m_a = e.pos; m_wa = m_wb = e.widgetPos; }
void ZoomTool::move(const ToolEvent& e) { if (m_drag) { m_wb = e.widgetPos; e.view->refresh(); } }
void ZoomTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    if (QLineF(m_wa, e.widgetPos).length() > 6 && !e.alt()) e.view->zoomToRect(QRectF(m_a, e.pos).normalized());
    else if (e.alt()) e.view->zoomOut(e.widgetPos);
    else e.view->zoomIn(e.widgetPos);
    e.view->refresh();
}
void ZoomTool::paintOverlay(QPainter& p, CanvasView*) {
    if (!m_drag) return;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 1, Qt::DashLine)); p.drawRect(QRectF(m_wa, m_wb).normalized());
    p.setPen(QPen(Qt::white, 1, Qt::DotLine)); p.drawRect(QRectF(m_wa, m_wb).normalized());
}

// ============================================================================ Recadrage
// handles : 1 HG, 2 H, 3 HD, 4 D, 5 BD, 6 B, 7 BG, 8 G ; 9 corps ; 10 création
static QPointF handlePoint(const QRectF& r, int h) {
    switch (h) {
    case 1: return r.topLeft(); case 2: return {r.center().x(), r.top()}; case 3: return r.topRight(); case 4: return {r.right(), r.center().y()};
    case 5: return r.bottomRight(); case 6: return {r.center().x(), r.bottom()}; case 7: return r.bottomLeft(); default: return {r.left(), r.center().y()};
    }
}

int CropTool::hit(const QPointF& w, CanvasView* v) const {
    if (m_rect.isEmpty()) return 0;
    QRectF wr = v->toWidget(m_rect);
    for (int h = 1; h <= 8; ++h) if (QLineF(w, handlePoint(wr, h)).length() <= 8) return h;
    return wr.contains(w) ? 9 : 0;
}

void CropTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    m_doc = e.doc;
    int h = hit(e.widgetPos, e.view);
    if (h == 0) { m_h = 10; m_start = e.pos; m_rect = QRectF(e.pos, QSizeF(0, 0)); }
    else { m_h = h; m_start = e.pos; m_r0 = m_rect; }
}

void CropTool::move(const ToolEvent& e) {
    if (m_h == 0 || !(e.buttons & Qt::LeftButton)) return;
    QPointF d = e.pos - m_start;
    if (m_h == 10) m_rect = QRectF(m_start, e.pos).normalized();
    else if (m_h == 9) m_rect = m_r0.translated(d);
    else {
        QRectF r = m_r0;
        if (m_h == 1 || m_h == 7 || m_h == 8) r.setLeft(m_r0.left() + d.x());
        if (m_h == 3 || m_h == 4 || m_h == 5) r.setRight(m_r0.right() + d.x());
        if (m_h == 1 || m_h == 2 || m_h == 3) r.setTop(m_r0.top() + d.y());
        if (m_h == 5 || m_h == 6 || m_h == 7) r.setBottom(m_r0.bottom() + d.y());
        m_rect = r.normalized();
    }
    e.view->refresh();
}

void CropTool::release(const ToolEvent& e) {
    m_h = 0;
    if (m_rect.width() < 2 || m_rect.height() < 2) m_rect = QRectF();
    e.view->refresh();
}

void CropTool::commit() {
    Document* d = Workspace::instance().doc();
    if (!d || m_rect.isEmpty()) return;
    QRect r = m_rect.toAlignedRect();
    m_rect = QRectF();
    Ops::cropTo(d, r);
}

bool CropTool::keyPress(QKeyEvent* k, CanvasView* v) {
    if (m_rect.isEmpty()) return false;
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { commit(); return true; }
    if (k->key() == Qt::Key_Escape) { m_rect = QRectF(); v->refresh(); return true; }
    return false;
}

void CropTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (m_rect.isEmpty()) return;
    QRectF wr = v->toWidget(m_rect), all = QRectF(v->viewport()->rect());
    QColor shade(0, 0, 0, 150);
    p.fillRect(QRectF(all.left(), all.top(), all.width(), wr.top() - all.top()), shade);
    p.fillRect(QRectF(all.left(), wr.bottom(), all.width(), all.bottom() - wr.bottom()), shade);
    p.fillRect(QRectF(all.left(), wr.top(), wr.left() - all.left(), wr.height()), shade);
    p.fillRect(QRectF(wr.right(), wr.top(), all.right() - wr.right(), wr.height()), shade);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 110), 1));
    for (int i = 1; i < 3; ++i) {
        p.drawLine(QPointF(wr.left() + wr.width() * i / 3, wr.top()), QPointF(wr.left() + wr.width() * i / 3, wr.bottom()));
        p.drawLine(QPointF(wr.left(), wr.top() + wr.height() * i / 3), QPointF(wr.right(), wr.top() + wr.height() * i / 3));
    }
    p.setPen(QPen(Qt::white, 1)); p.drawRect(wr);
    p.setPen(Qt::black); p.setBrush(Qt::white);
    for (int h = 1; h <= 8; ++h) { QPointF c = handlePoint(wr, h); p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8)); }
    p.setPen(Qt::white);
    p.drawText(wr.topLeft() + QPointF(4, -6), QString("%1 × %2").arg(int(m_rect.width())).arg(int(m_rect.height())));
}
