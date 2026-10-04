#include "Tools.h"
#include "core/MatUtil.h"
#include "core/Operations.h"
#include "ui/CanvasView.h"
#include "ui/OptionsBar.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

// ============================================================================ Pipette
QColor EyedropperTool::colorAt(Document* d, QPointF p, int size, bool merged) {
    auto l = d->activeLayer();
    const cv::Mat* src = merged ? &d->composite() : (l ? &l->image : nullptr);
    if (!src) return Qt::black;
    int cx = int(std::floor(p.x())), cy = int(std::floor(p.y())), h = size / 2;
    long b = 0, g = 0, r = 0; int n = 0;
    for (int y = cy - h; y <= cy + h; ++y)
        for (int x = cx - h; x <= cx + h; ++x)
            if (x >= 0 && y >= 0 && x < src->cols && y < src->rows) { auto v = src->at<cv::Vec4b>(y, x); b += v[0]; g += v[1]; r += v[2]; ++n; }
    return n ? QColor(int(r / n), int(g / n), int(b / n)) : QColor(Qt::black);
}

void EyedropperTool::buildOptions(OptionsBar& o) {
    o.addCombo(tr("Taille de l'échantillon :"), {tr("Point"), tr("Moyenne 3×3"), tr("Moyenne 5×5"), tr("Moyenne 11×11")}, &st().sampleSize);
    o.addCheck(tr("Tous les calques"), &st().sampleAll);
    o.addLabel(tr("   Alt+clic : couleur d'arrière-plan"));
}

void EyedropperTool::sample(const ToolEvent& e) {
    static const int sizes[] = {1, 3, 5, 11};
    QColor c = colorAt(e.doc, e.pos, sizes[std::clamp(st().sampleSize, 0, 3)], st().sampleAll);
    // (sampleSize stocke l'index du combo)
    if (e.alt()) ws().setBg(c); else ws().setFg(c);
}

// ============================================================================ Pinceau & assimilés
QString PaintTool::id() const {
    static const char* n[] = {"brush", "pencil", "eraser", "clone", "blur", "sharpen", "smudge", "dodge", "burn"};
    return n[m_kind];
}
QString PaintTool::name() const {
    static const char* n[] = {QT_TR_NOOP("Pinceau (B)"), QT_TR_NOOP("Crayon (B)"), QT_TR_NOOP("Gomme (E)"), QT_TR_NOOP("Tampon de duplication (S)"), QT_TR_NOOP("Goutte d'eau (R)"),
                              QT_TR_NOOP("Netteté (R)"), QT_TR_NOOP("Doigt (R)"), QT_TR_NOOP("Densité - (O)"), QT_TR_NOOP("Densité + (O)")};
    return tr(n[m_kind]);
}

BrushEngine::Params PaintTool::params() const {
    BrushEngine::Params p;
    p.size = st().size; p.hardness = st().hardness / 100.0; p.opacity = st().opacity / 100.0; p.flow = st().flow / 100.0;
    p.spacing = st().spacing / 100.0; p.strength = st().strength / 100.0; p.range = st().range; p.color = ws().fg;
    switch (m_kind) {
    case Brush: p.mode = BrushEngine::Mode::Paint; break;
    case Pencil: p.mode = BrushEngine::Mode::Paint; p.pencil = true; break;
    case Eraser: p.mode = BrushEngine::Mode::Erase; break;
    case Clone: p.mode = BrushEngine::Mode::Clone; break;
    case Blur: p.mode = BrushEngine::Mode::Blur; break;
    case Sharpen: p.mode = BrushEngine::Mode::Sharpen; break;
    case Smudge: p.mode = BrushEngine::Mode::Smudge; break;
    case Dodge: p.mode = BrushEngine::Mode::Dodge; break;
    case Burn: p.mode = BrushEngine::Mode::Burn; break;
    }
    return p;
}

void PaintTool::buildOptions(OptionsBar& o) {
    o.addSpin(tr("Taille :"), &st().size, 1, 2000, " px");
    if (m_kind != Pencil) o.addSpin(tr("Dureté :"), &st().hardness, 0, 100, " %");
    if (m_kind == Brush || m_kind == Pencil || m_kind == Eraser || m_kind == Clone) {
        o.addSpin(tr("Opacité :"), &st().opacity, 1, 100, " %");
        o.addSpin(tr("Flux :"), &st().flow, 1, 100, " %");
    }
    o.addSpin(tr("Espacement :"), &st().spacing, 1, 200, " %");
    if (m_kind == Blur || m_kind == Sharpen || m_kind == Smudge || m_kind == Dodge || m_kind == Burn) o.addSpin(tr("Force :"), &st().strength, 1, 100, " %");
    if (m_kind == Dodge || m_kind == Burn) o.addCombo(tr("Plage :"), {tr("Ombres"), tr("Tons moyens"), tr("Hautes lumières")}, &st().range);
    if (m_kind == Clone) { o.addCheck(tr("Aligné"), &st().aligned); o.addLabel(tr("  Alt+clic : définir la source")); }
    if (m_kind == Brush || m_kind == Pencil) o.addLabel(tr("  Alt : pipette • Maj+clic : ligne droite • [ ] : taille • { } : dureté"));
}

void PaintTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    m_hover = e.pos; m_hasHover = true;
    if (e.alt()) {
        if (m_kind == Clone) { m_cloneSrc = QPoint(int(e.pos.x()), int(e.pos.y())); m_hasSrc = true; m_offsetSet = false; e.view->refresh(); }
        else if (m_kind == Brush || m_kind == Pencil) ws().setFg(EyedropperTool::colorAt(e.doc, e.pos, 1, true));
        return;
    }
    QString why;
    auto l = e.doc->editableLayer(&why);
    if (!l) { ws().message(why); return; }
    Document::Target t = (l->editingMask && l->hasMask()) ? Document::Target::Mask : Document::Target::Pixels;
    if (t == Document::Target::Pixels && l->isText()) { Ops::rasterizeText(e.doc); ws().message(tr("Calque de texte pixellisé.")); }
    BrushEngine::Params p = params();
    if (m_kind == Clone) {
        if (!m_hasSrc) { ws().message(tr("Tampon : Alt+clic pour définir le point source.")); return; }
        if (!m_offsetSet || !st().aligned) { m_cloneOffset = m_cloneSrc - QPoint(int(e.pos.x()), int(e.pos.y())); m_offsetSet = true; }
        p.cloneOffset = m_cloneOffset;
    }
    if (!m_engine.begin(e.doc, l, t, p)) return;
    if (e.shift() && m_hasLast && (m_kind == Brush || m_kind == Pencil || m_kind == Eraser)) m_engine.strokeTo(m_lastEnd);
    m_engine.strokeTo(e.pos);
    m_lastEnd = e.pos; m_hasLast = true;
}

void PaintTool::move(const ToolEvent& e) {
    m_hover = e.pos; m_hasHover = true;
    if (m_engine.active() && (e.buttons & Qt::LeftButton)) { m_engine.strokeTo(e.pos); m_lastEnd = e.pos; }
    e.view->refresh();
}

void PaintTool::release(const ToolEvent& e) {
    if (m_engine.active()) m_engine.end(name().section(" (", 0, 0));
    if (m_kind == Clone && m_hasSrc && !st().aligned) m_offsetSet = false;
    e.view->refresh();
}

bool PaintTool::keyPress(QKeyEvent* k, CanvasView* v) {
    QString t = k->text();
    auto bump = [&](int& val, int delta, int lo, int hi) { val = std::clamp(val + delta, lo, hi); ws().settingsModified(); v->refresh(); };
    if (t == "[") { bump(st().size, -(st().size > 100 ? 20 : st().size > 20 ? 5 : 1), 1, 2000); return true; }
    if (t == "]") { bump(st().size, (st().size >= 100 ? 20 : st().size >= 20 ? 5 : 1), 1, 2000); return true; }
    if (t == "{") { bump(st().hardness, -10, 0, 100); return true; }
    if (t == "}") { bump(st().hardness, 10, 0, 100); return true; }
    return false;
}

void PaintTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_hasHover) return;
    QPointF c = v->toWidget(m_hover);
    double r = st().size * v->zoom() / 2.0;
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    if (r >= 2) {
        p.setPen(QPen(QColor(0, 0, 0, 200), 1.5)); p.drawEllipse(c, r + 0.5, r + 0.5);
        p.setPen(QPen(QColor(255, 255, 255, 230), 1)); p.drawEllipse(c, r, r);
    }
    if (r < 12) {
        p.setPen(QPen(Qt::black, 3)); p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0)); p.drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
        p.setPen(QPen(Qt::white, 1)); p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0)); p.drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
    }
    if (m_kind == Clone && m_hasSrc) {
        QPointF s = m_offsetSet ? v->toWidget(m_hover + QPointF(m_cloneOffset)) : v->toWidget(QPointF(m_cloneSrc));
        p.setPen(QPen(QColor(255, 0, 80), 1.5));
        p.drawEllipse(s, std::max(4.0, r), std::max(4.0, r));
        p.drawLine(s + QPointF(-8, 0), s + QPointF(8, 0)); p.drawLine(s + QPointF(0, -8), s + QPointF(0, 8));
    }
}

// ============================================================================ Pot de peinture
void BucketTool::buildOptions(OptionsBar& o) {
    o.addSpin(tr("Tolérance :"), &st().tolerance, 0, 255);
    o.addSpin(tr("Opacité :"), &st().opacity, 1, 100, " %");
    o.addCheck(tr("Lissage"), &st().antiAlias);
    o.addCheck(tr("Contigus"), &st().contiguous);
    o.addCheck(tr("Tous les calques"), &st().sampleAll);
}

void BucketTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    auto l = e.doc->activeLayer();
    if (!l) return;
    const cv::Mat& src = st().sampleAll ? e.doc->composite() : l->image;
    cv::Mat m = Sel::magicWand(src, cv::Point(int(std::floor(e.pos.x())), int(std::floor(e.pos.y()))), st().tolerance, st().contiguous, st().antiAlias);
    if (m.empty()) return;
    Ops::fillMask(e.doc, m, ws().fg, st().opacity / 100.0, tr("Pot de peinture"));
}

// ============================================================================ Dégradé
void GradientTool::buildOptions(OptionsBar& o) {
    o.addCombo(tr("Type :"), {tr("Linéaire"), tr("Radial"), tr("Angulaire"), tr("Réfléchi"), tr("Losange")}, &st().gradientType);
    o.addCombo(tr("Couleurs :"), {tr("Premier plan → Arrière-plan"), tr("Premier plan → Transparent")}, &st().gradientColors);
    o.addCheck(tr("Inverser"), &st().gradientReverse);
    o.addSpin(tr("Opacité :"), &st().opacity, 1, 100, " %");
}

void GradientTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag) return;
    QPointF a = v->toWidget(m_a), b = v->toWidget(m_b);
    p.setPen(QPen(Qt::black, 3)); p.drawLine(a, b);
    p.setPen(QPen(Qt::white, 1)); p.drawLine(a, b);
    p.setBrush(Qt::white); p.drawEllipse(a, 3, 3);
}

void GradientTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    e.view->refresh();
    QString why;
    auto l = e.doc->editableLayer(&why);
    if (!l) { ws().message(why); return; }
    QPointF a = m_a, b = e.pos;
    double vx = b.x() - a.x(), vy = b.y() - a.y(), len2 = vx * vx + vy * vy;
    if (len2 < 1) return;
    cv::Rect region = e.doc->hasSelection() ? Sel::bounds(e.doc->selection()) : mu::bounds(l->image);
    if (region.empty()) return;
    QColor c0 = ws().fg, c1 = st().gradientColors == 0 ? ws().bg : QColor(ws().fg.red(), ws().fg.green(), ws().fg.blue(), 0);
    if (st().gradientReverse) std::swap(c0, c1);
    const int type = st().gradientType;
    const double len = std::sqrt(len2), a0 = std::atan2(vy, vx);
    cv::Mat grad(region.height, region.width, CV_8UC4);
    for (int y = 0; y < region.height; ++y) {
        cv::Vec4b* row = grad.ptr<cv::Vec4b>(y);
        for (int x = 0; x < region.width; ++x) {
            double px = region.x + x + 0.5 - a.x(), py = region.y + y + 0.5 - a.y(), t = 0;
            switch (type) {
            case 0: t = (px * vx + py * vy) / len2; break;
            case 1: t = std::hypot(px, py) / len; break;
            case 2: { t = (std::atan2(py, px) - a0) / (2 * CV_PI); t -= std::floor(t); break; }
            case 3: t = std::fabs((px * vx + py * vy) / len2); break;
            default: t = (std::fabs(px) + std::fabs(py)) / (std::fabs(vx) + std::fabs(vy)); break;
            }
            t = std::clamp(t, 0.0, 1.0);
            auto mix = [&](int u, int w) { return uchar(std::lround(u + (w - u) * t)); };
            row[x] = cv::Vec4b(mix(c0.blue(), c1.blue()), mix(c0.green(), c1.green()), mix(c0.red(), c1.red()), mix(c0.alpha(), c1.alpha()));
        }
    }
    double op = st().opacity / 100.0;
    e.doc->doLayerChange(tr("Dégradé"), l, [&](Layer& L) {
        cv::Mat out = L.image.clone();
        Blend::over(out, grad, region.tl(), region, BlendMode::Normal, float(op), cv::Mat(), e.doc->selection());
        L.image = out; L.text.reset();
    });
}
