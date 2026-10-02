#include "Tools.h"
#include "core/Selection.h"
#include "ui/CanvasView.h"
#include "ui/OptionsBar.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

bool SelectionTransformTool::begin() {
    endSession();
    Document* d = ws().doc();
    CanvasView* v = ws().view();
    if (!d || !d->hasSelection()) { if (d) ws().message(tr("Transformation de la sélection : faites d'abord une sélection.")); return false; }
    cv::Rect r = Sel::bounds(d->selection());
    m_contours = Sel::contours(d->selection());
    m_box.reset(QRectF(r.x, r.y, r.width, r.height));
    m_doc = d; m_view = v; m_active = true; m_dirty = false; m_contentFailed = false;
    connect(d, &Document::selectionChanged, this, &SelectionTransformTool::onSelectionChanged, Qt::UniqueConnection);
    if (v) { v->suppressAnts = true; v->refresh(); }       // on dessine nous-mêmes le contour (transformé)
    syncSpins();
    ws().message(st().selTransformContent ? tr("Sélection + pixels du calque actif : poignées = échelle • hors du cadre = rotation • Entrée = valider")
                                          : tr("Contour seul : poignées = échelle • hors du cadre = rotation • Entrée = valider"));
    return true;
}

void SelectionTransformTool::endSession() {
    if (m_fc.active()) m_fc.cancel();                       // abandon : restitue les pixels d'origine
    if (m_doc) disconnect(m_doc.data(), nullptr, this, nullptr);
    if (m_view) { m_view->suppressAnts = false; m_view->refresh(); }
    m_active = false; m_dirty = false;
    m_doc = nullptr; m_view = nullptr; m_contours.clear();
}

// Soulève les pixels sélectionnés du calque actif au premier changement (session inactive = aucune copie coûteuse).
bool SelectionTransformTool::ensureContent() {
    if (m_fc.active()) return true;
    if (!m_doc || m_contentFailed) return false;
    if (m_fc.begin(m_doc)) return true;
    m_contentFailed = true;
    ws().message(tr("Calque masqué ou verrouillé : seul le contour de la sélection est transformé."));
    return false;
}

void SelectionTransformTool::boxChanged() {
    m_dirty = true;
    if (st().selTransformContent && ensureContent()) m_fc.update(m_box.matrix());     // aperçu en direct des pixels
    syncSpins();
    if (m_view) m_view->refresh();
}

void SelectionTransformTool::commit(bool restart) {
    if (!m_active || !m_doc) return;
    Document* d = m_doc;
    if (m_dirty) {
        m_committing = true;
        if (m_fc.active()) {
            m_fc.commit(tr("Transformation de la sélection"));           // pixels + contour = une seule étape d'historique
        } else {
            cv::Mat out;
            cv::warpAffine(d->selection(), out, m_box.matrix(), d->selection().size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            d->setSelection(out, tr("Transformation de la sélection"));
        }
        m_committing = false;
    }
    endSession();                                                    // (annule le contenu soulevé s'il n'y a eu aucun changement)
    if (restart) begin();                                            // nouvelles poignées autour de la sélection transformée
}

void SelectionTransformTool::onSelectionChanged() {
    if (m_committing || !m_active) return;
    begin();                   // sélection modifiée de l'extérieur (annuler/rétablir…) : on repart de zéro
}

void SelectionTransformTool::press(const ToolEvent& e) {
    if (e.button != Qt::LeftButton || !m_active) return;
    m_box.press(e);
}

void SelectionTransformTool::move(const ToolEvent& e) {
    if (!m_active) return;
    if (m_box.move(e)) boxChanged();
}

bool SelectionTransformTool::keyPress(QKeyEvent* k, CanvasView*) {
    if (!m_active) return false;
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { commit(true); return true; }
    if (k->key() == Qt::Key_Escape) { endSession(); begin(); return true; }
    return false;
}

void SelectionTransformTool::paintOverlay(QPainter& p, CanvasView* v) {
    if (!m_active) return;
    p.setBrush(Qt::NoBrush);
    QPen white(Qt::white, 1), black(Qt::black, 1, Qt::DashLine);
    for (const auto& c : m_contours) {
        QPolygonF poly;
        for (const cv::Point& pt : c) poly << v->toWidget(m_box.apply(QPointF(pt.x + 0.5, pt.y + 0.5)));
        if (poly.size() < 2) continue;
        p.setPen(white); p.drawPolygon(poly);
        p.setPen(black); p.drawPolygon(poly);
    }
    m_box.paint(p, v);
}

// ------------------------------------------------------------------------------ barre d'options (contenu + saisie numérique)
void SelectionTransformTool::buildOptions(OptionsBar& o) {
    auto* content = new QCheckBox(tr("Transformer le contenu du calque"));
    content->setToolTip(tr("Coché : les pixels du calque actif situés dans la sélection suivent la transformation.\nDécoché : seul le contour de la sélection est transformé."));
    content->setChecked(st().selTransformContent);
    connect(content, &QCheckBox::toggled, this, [this](bool on) {
        if (m_active && m_dirty) commit(true);            // valide ce qui a été fait avant de changer de mode
        st().selTransformContent = on;
        if (m_active) begin();
    });
    o.addWidget(content);
    o.addSeparator();
    auto mk = [&](double mn, double mx, const QString& suffix, int dec) {
        auto* s = new QDoubleSpinBox; s->setRange(mn, mx); s->setDecimals(dec); s->setSuffix(suffix); s->setKeyboardTracking(false); s->setFixedWidth(92);
        o.addWidget(s);
        return s;
    };
    o.addLabel(tr("L :"));  m_spX = mk(-5000, 5000, " %", 1);
    m_lock = new QCheckBox(tr("Proportions")); m_lock->setChecked(true); o.addWidget(m_lock);
    o.addLabel(tr("H :"));  m_spY = mk(-5000, 5000, " %", 1);
    o.addLabel(tr("Rotation :")); m_spA = mk(-360, 360, " °", 1);
    connect(m_spX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { applyNumeric(true); });
    connect(m_spY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { applyNumeric(false); });
    connect(m_spA, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { applyNumeric(true); });
    o.addSeparator();
    o.addButton(tr("Valider"), [this] { commit(true); });
    o.addButton(tr("Annuler"), [this] { endSession(); begin(); });
    syncSpins();
}

void SelectionTransformTool::syncSpins() {
    if (!m_spX || !m_spY || !m_spA) return;
    m_syncing = true;
    m_spX->setValue(m_box.scaleX() * 100.0);
    m_spY->setValue(m_box.scaleY() * 100.0);
    double a = std::fmod(m_box.angle() * 180.0 / CV_PI, 360.0);
    if (a > 180) a -= 360;
    if (a < -180) a += 360;
    m_spA->setValue(a);
    m_syncing = false;
}

void SelectionTransformTool::applyNumeric(bool fromX) {
    if (m_syncing || !m_active || !m_spX || !m_spY || !m_spA) return;
    double sx = m_spX->value() / 100.0, sy = m_spY->value() / 100.0;
    if (std::fabs(sx) < 0.01) sx = 0.01;
    if (std::fabs(sy) < 0.01) sy = 0.01;
    if (m_lock && m_lock->isChecked()) { if (fromX) sy = sx; else sx = sy; }
    m_box.setScale(sx, sy);
    m_box.setAngle(m_spA->value() * CV_PI / 180.0);
    boxChanged();
}
