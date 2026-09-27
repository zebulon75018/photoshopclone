#pragma once
#include "MovableDialog.h"
#include <QTimer>
#include "core/Document.h"
#include "effects/Effect.h"

// Boîte de dialogue générique d'un effet : contrôles générés depuis ParamDef, aperçu en direct sur le calque.
// L'application définitive est faite par l'appelant (Ops::applyEffect) pour garantir un historique propre.
class EffectDialog : public MovableDialog {
    Q_OBJECT
public:
    EffectDialog(Document* doc, EffectPtr effect, const Params& initial, QWidget* parent);
    Params params() const { return m_params; }
    void accept() override;
    void reject() override;
private:
    void buildUi();
    void schedulePreview();
    void preview();
    void restore();
    Document* m_doc; EffectPtr m_effect; Params m_params; Layer::Ptr m_layer; cv::Mat m_orig;
    QTimer m_timer; bool m_preview = true;
    std::vector<std::function<void()>> m_resetters;
};
