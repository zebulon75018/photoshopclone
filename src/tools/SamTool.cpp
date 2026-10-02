#include "Tools.h"
#include "ai/AIBackend.h"
#include "core/Selection.h"
#include "ui/CanvasView.h"
#include "ui/OptionsBar.h"
#include <QApplication>
#include <cmath>

static Sel::Mode samMode(const ToolEvent& e) {
    if (e.shift() && e.alt()) return Sel::Mode::Intersect;
    if (e.shift()) return Sel::Mode::Add;
    if (e.alt()) return Sel::Mode::Subtract;
    return Workspace::instance().settings.selMode;
}

void SamTool::activate() {
    m_stale = true;
    ws().message(AIBackend::available()
                     ? tr("Sélection par IA : cliquez sur un objet, ou tracez un cadre autour. Maj = ajouter, Alt = soustraire. (1re utilisation : analyse de l'image, quelques secondes)")
                     : tr("Sélection par IA indisponible : PhotoClone a été compilé sans vision.cpp."));
}

void SamTool::buildOptions(OptionsBar& o) {
    o.addSelectionModes(&st().selMode);
    o.addSeparator();
    o.addSpin(tr("Contour progressif :"), &st().feather, 0, 250, " px");
    o.addCheck(tr("Analyser tous les calques"), &st().samSampleAll);
    o.addButton(tr("Ré-analyser l'image"), [this] { m_stale = true; ws().message(tr("L'image sera ré-analysée au prochain clic.")); });
    o.addLabel(tr("  Modèle MobileSAM : IA > Réglages des modèles…"));
}

// Encode l'image courante si nécessaire (première fois, document/calque modifié, ou demande explicite).
bool SamTool::ensureEncoded(Document* d, CanvasView* v) {
    if (m_doc != d) {
        if (m_doc) disconnect(m_doc.data(), nullptr, this, nullptr);
        m_doc = d;
        connect(d, &Document::changed, this, [this] { m_stale = true; });     // pixels modifiés : l'encodage n'est plus fiable
        m_stale = true;
    }
    if (m_lastSampleAll != st().samSampleAll) m_stale = true;   // la source d'analyse a changé (tous les calques / calque actif)
    if (!m_stale && AIBackend::samReady()) return true;
    auto layer = d->activeLayer();
    if (!st().samSampleAll && !layer) return false;
    cv::Mat src = st().samSampleAll ? d->compositeCopy() : layer->image;
    ws().message(tr("Analyse de l'image par MobileSAM…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    QString err;
    bool ok = AIBackend::samEncode(src, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) { ws().message(err); return false; }
    m_stale = false;
    m_lastSampleAll = st().samSampleAll;
    return true;
}

void SamTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton) return;
    m_drag = true; m_a = m_b = e.pos; m_wa = m_wb = e.widgetPos;
}

void SamTool::move(const ToolEvent& e) {
    if (!m_drag) return;
    m_b = e.pos; m_wb = e.widgetPos;
    e.view->refresh();
}

void SamTool::release(const ToolEvent& e) {
    if (!m_drag) return;
    m_drag = false;
    e.view->refresh();
    const bool isBox = QLineF(m_wa, e.widgetPos).length() > 6.0;
    if (!ensureEncoded(e.doc, e.view)) return;
    AIBackend::Result r;
    if (isBox) {
        QRectF f = QRectF(m_a, e.pos).normalized();
        r = AIBackend::samComputeBox(QRect(int(std::floor(f.left())), int(std::floor(f.top())), int(std::ceil(f.width())), int(std::ceil(f.height()))));
    } else {
        r = AIBackend::samComputePoint(QPoint(int(std::floor(e.pos.x())), int(std::floor(e.pos.y()))));
    }
    if (!r.ok) { ws().message(r.error); return; }
    if (r.data.size() != cv::Size(e.doc->size().width(), e.doc->size().height())) { ws().message(tr("MobileSAM : masque de taille inattendue.")); return; }
    cv::Mat mask = r.data;
    if (st().feather > 0) mask = Sel::feather(mask, st().feather);
    if (cv::countNonZero(mask) == 0) { ws().message(tr("MobileSAM : aucun objet détecté à cet endroit.")); return; }
    e.doc->setSelection(Sel::combine(e.doc->selection(), mask, samMode(e), e.doc->size()), tr("Sélection par IA"));
}

void SamTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_drag || QLineF(m_wa, m_wb).length() <= 6.0) return;
    QRectF r = QRectF(m_wa, m_wb).normalized();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 3)); p.drawRect(r);
    p.setPen(QPen(Qt::white, 1, Qt::DashLine)); p.drawRect(r);
}
