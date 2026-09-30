#pragma once
// Boîtes de dialogue des fonctions IA (vision.cpp) : réglages des modèles, suppression de l'arrière-plan, carte de
// profondeur, agrandissement. Toutes dérivent de MovableDialog (déplaçables dans la fenêtre principale).
//
// Principe commun : le réseau de neurones (lent) n'est lancé qu'UNE fois à l'ouverture (ou sur « Recalculer ») ; tous les
// autres réglages ne rejouent que des post-traitements OpenCV très rapides (src/ai/MaskRefine.h) sur le résultat en
// cache, ce qui permet un aperçu en direct fluide. L'application définitive est faite par l'appelant (Ops::…).
#include <functional>
#include "MovableDialog.h"
#include "ai/AIBackend.h"
#include "core/Document.h"
#include "core/Operations.h"

class QLabel; class QCheckBox; class QComboBox; class QPushButton; class QTimer; class QDoubleSpinBox; class QSpinBox;
class SliderSpin;

// Fonction qui calcule le résultat brut du modèle sur une image BGRA. Par défaut : AIBackend::* ; injectable pour les tests.
using AIImageFn = std::function<AIBackend::Result(const cv::Mat&)>;

class AIModelsDialog : public MovableDialog {
    Q_OBJECT
public:
    explicit AIModelsDialog(QWidget* parent);
    void accept() override;
private:
    struct Row { AIArchitecture arch; class QLineEdit* path; QLabel* status; };
    void refreshRow(Row&);
    std::vector<Row> m_rows;
};

class RemoveBackgroundDialog : public MovableDialog {
    Q_OBJECT
public:
    RemoveBackgroundDialog(Document* doc, QWidget* parent, AIImageFn provider = {});
    Ops::RemoveBgParams params() const;
    cv::Mat rawMask() const { return m_raw; }
    bool hasMask() const { return !m_raw.empty(); }
    void accept() override;
    void reject() override;
private:
    void compute();
    void schedulePreview();
    void preview();
    void restore();
    void updateEnabled();
    Document* m_doc; Layer::Ptr m_layer; cv::Mat m_orig, m_raw; AIImageFn m_provider;
    QTimer* m_timer; bool m_preview = true;
    QCheckBox *m_sampleAll, *m_keepLargest, *m_invert, *m_defringe, *m_previewCb;
    SliderSpin *m_threshold, *m_shift, *m_feather, *m_defringeRadius;
    QComboBox* m_output; QLabel* m_status; QPushButton *m_ok, *m_recompute;
};

class DepthDialog : public MovableDialog {
    Q_OBJECT
public:
    DepthDialog(Document* doc, QWidget* parent, AIImageFn provider = {});
    Ops::DepthApplyParams params() const;
    cv::Mat rawDepth() const { return m_raw; }
    bool hasDepth() const { return !m_raw.empty(); }
private:
    void compute();
    void refreshThumb();
    void updateEnabled();
    Document* m_doc; cv::Mat m_raw; AIImageFn m_provider;
    QCheckBox *m_sampleAll, *m_invert, *m_auto, *m_makeLayer, *m_makeSel;
    SliderSpin *m_smooth, *m_threshold, *m_feather;
    QComboBox* m_selMode; QLabel *m_thumb, *m_status; QPushButton *m_ok, *m_recompute;
};

class UpscaleDialog : public MovableDialog {
    Q_OBJECT
public:
    UpscaleDialog(Document* doc, QWidget* parent);
    Ops::UpscaleParams params() const;
private:
    void refreshInfo();
    Document* m_doc; QCheckBox* m_native; QDoubleSpinBox* m_factor; QLabel* m_info;
};
