#include "Document.h"
#include "Commands.h"
#include "MatUtil.h"
#include <QFileInfo>
#include <algorithm>

Document::Document(QSize size, QObject* parent) : QObject(parent), m_size(size) { rebuildComposite(); }

QString Document::title() const { return path.isEmpty() ? tr("Sans titre") : QFileInfo(path).fileName(); }

void Document::setActiveIndex(int i) {
    i = std::clamp(i, 0, std::max(0, int(m_layers.size()) - 1));
    if (i == m_active) return;
    m_active = i;
    emit activeLayerChanged();
}

Layer::Ptr Document::activeLayer() const {
    return (m_active >= 0 && m_active < int(m_layers.size())) ? m_layers[m_active] : nullptr;
}

Layer::Ptr Document::layerById(int id) const {
    for (auto& l : m_layers) if (l->id() == id) return l;
    return nullptr;
}

int Document::indexOf(const Layer* l) const {
    for (size_t i = 0; i < m_layers.size(); ++i) if (m_layers[i].get() == l) return int(i);
    return -1;
}

Layer::Ptr Document::editableLayer(QString* why) const {
    auto l = activeLayer();
    if (!l) { if (why) *why = tr("Aucun calque actif."); return nullptr; }
    if (!l->props.visible) { if (why) *why = tr("Le calque cible est masqué."); return nullptr; }
    if (l->props.locked) { if (why) *why = tr("Le calque cible est verrouillé."); return nullptr; }
    return l;
}

// ------------------------------------------------------------------ composite
void Document::rebuildComposite() {
    m_comp = mu::newMat(m_size);
    m_display = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
    recomposite(rect());
}

void Document::invalidate(const QRect& r0) {
    QRect r = r0 & rect();
    if (r.isEmpty()) return;
    recomposite(r);
    emit changed(r);
}

void Document::recomposite(const QRect& r) {
    cv::Rect cr = mu::toCv(r);
    m_comp(cr).setTo(cv::Scalar(0, 0, 0, 0));
    for (const auto& l : m_layers) {
        if (!l->props.visible || l->image.size() != m_comp.size()) continue;
        Blend::over(m_comp, l->image, {0, 0}, cr, l->props.blend, l->props.opacity,
                    l->props.maskEnabled ? l->mask : cv::Mat());
    }
#pragma omp parallel for
    for (int y = cr.y; y < cr.y + cr.height; ++y) {
        const cv::Vec4b* s = m_comp.ptr<cv::Vec4b>(y);
        uchar* d = m_display.scanLine(y);
        for (int x = cr.x; x < cr.x + cr.width; ++x) {
            int a = s[x][3];
            uchar* o = d + x * 4;
            if (a == 255) { o[0] = s[x][0]; o[1] = s[x][1]; o[2] = s[x][2]; o[3] = 255; }
            else { o[0] = uchar((s[x][0] * a + 127) / 255); o[1] = uchar((s[x][1] * a + 127) / 255); o[2] = uchar((s[x][2] * a + 127) / 255); o[3] = uchar(a); }
        }
    }
}

// ------------------------------------------------------------------ sélection
void Document::setSelection(const cv::Mat& s, const QString& name) {
    cv::Mat n = (s.empty() || cv::countNonZero(s) == 0) ? cv::Mat() : s;
    if (m_sel.empty() && n.empty()) return;
    m_undo.push(new SelectionCommand(this, m_sel, n, name));
}

void Document::applySelection(const cv::Mat& s) {
    if (!m_sel.empty()) m_lastSel = m_sel;
    m_sel = s;
    emit selectionChanged();
}

void Document::reselect() { if (!m_lastSel.empty() && m_sel.empty()) setSelection(m_lastSel, tr("Resélectionner")); }

// ------------------------------------------------------------------ historique
void Document::applyStructure(const Structure& s) {
    bool resized = s.size != m_size;
    m_layers = s.layers;
    m_active = s.active;
    if (resized) {
        m_size = s.size;
        m_sel = cv::Mat();
        m_lastSel = cv::Mat();
        rebuildComposite();
        emit selectionChanged();
        emit sizeChanged();
    } else {
        recomposite(rect());
        emit changed(rect());
    }
    emit layersChanged();
    emit activeLayerChanged();
}

void Document::doStructural(const QString& name, const std::function<void()>& change) {
    Structure before = structure();
    QSize oldSize = m_size;
    change();
    Structure after = structure();
    m_size = oldSize;             // applyStructure gère le changement de taille lui-même
    m_undo.push(new StructureCommand(this, before, after, name));
}

void Document::applyLayerState(const Layer::Ptr& l, const LayerState& s) {
    l->setState(s);
    invalidateAll();
    emit layersChanged();
}

void Document::pushLayerChange(const QString& name, const Layer::Ptr& l, const LayerState& before) {
    m_undo.push(new LayerStateCommand(this, l, before, l->state(), name));
}

void Document::doLayerChange(const QString& name, const Layer::Ptr& l, const std::function<void(Layer&)>& change) {
    LayerState before = l->state();
    change(*l);
    pushLayerChange(name, l, before);
}

cv::Mat Document::beginStroke(const Layer::Ptr& l, Target t) const {
    return (t == Target::Mask ? l->mask : l->image).clone();
}

void Document::endStroke(const QString& name, const Layer::Ptr& l, Target t, const cv::Mat& snap, const QRect& dirty) {
    cv::Rect r = mu::toCv(dirty) & mu::bounds(snap);
    if (r.empty()) return;
    const cv::Mat& cur = t == Target::Mask ? l->mask : l->image;
    m_undo.push(new PixelCommand(this, l, t, r, snap(r).clone(), cur(r).clone(), name));
}
