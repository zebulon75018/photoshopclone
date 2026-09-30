#pragma once
// Opérations de haut niveau sur un Document (chaque appel = une entrée d'historique).
// Aucune dépendance à l'interface graphique : testable et réutilisable (scripts, macros...).
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include "Document.h"
#include "effects/Effect.h"
#include "ai/MaskRefine.h"
#include "ai/SdImaging.h"

namespace Ops {
Document* makeDocument(QSize size, int background /*0 blanc, 1 couleur, 2 transparent*/, const QColor& bg);

// --- Calques
Layer::Ptr addLayer(Document*, const QString& name = QString());
void addLayerWithImage(Document*, const cv::Mat& bgra, const QString& name);
void duplicateLayer(Document*);
void layerViaCopy(Document*, bool cut);      // Ctrl+J / Ctrl+Maj+J
void deleteLayer(Document*);
void mergeDown(Document*);
void mergeVisible(Document*);
void flatten(Document*);
void reorderByIds(Document*, const std::vector<int>& idsBottomToTop);
void moveLayer(Document*, int delta);            // +1 vers le haut, -1 vers le bas
void moveLayerToEnd(Document*, bool top);
void setProps(Document*, const Layer::Ptr&, const LayerProps&, const QString& undoName);
void addMask(Document*, bool hideAll);
void deleteMask(Document*, bool apply);
void flipLayer(Document*, bool horizontal);
Layer::Ptr commitText(Document*, const Layer::Ptr& existing, const TextData& td);
void rasterizeText(Document*);

// --- Image
void resizeImage(Document*, QSize, int cvInterpolation);
void resizeCanvas(Document*, QSize, int anchor /*0..8, ligne par ligne*/);
void cropTo(Document*, const QRect&);
void rotateImage(Document*, int degreesCW);     // 90, 180, 270
void flipImage(Document*, bool horizontal);

// --- Sélection
void selectAll(Document*);
void deselect(Document*);
void invertSelection(Document*);
void modifySelection(Document*, int kind /*0 adoucir(feather) 1 étendre 2 contracter 3 lisser*/, double amount);

// --- Édition
void fillSelection(Document*, const QColor&, const QString& undoName);
void clearSelection(Document*);
void copy(Document*, bool merged);
void cut(Document*);
Layer::Ptr paste(Document*, bool inPlace, QPoint center);
void applyEffect(Document*, const Effect&, const Params&);
void fillMask(Document*, const cv::Mat& mask, const QColor&, double opacity, const QString& undoName);

// --- IA (voir src/ai/) : reçoivent un résultat de modèle DÉJÀ calculé (masque brut, carte de profondeur), ce qui les rend
//     testables sans fichier de modèle et permet à l'interface de ne lancer le réseau (lent) qu'une seule fois.
struct RemoveBgParams {
    enum Output { LayerMask, NewLayer, ReplaceLayer, SelectionOnly };
    MaskRefineParams mask;
    Output output = LayerMask;   // LayerMask : non destructif (masque de fusion) ; NewLayer : sujet sur un nouveau calque ;
                                 // ReplaceLayer : transparence appliquée aux pixels du calque actif ; SelectionOnly : sélection seule
    bool defringe = true;        // corrige les couleurs de bord (NewLayer / ReplaceLayer seulement)
    int defringeRadius = 30;
    bool sampleAll = false;      // NewLayer : découpe le sujet dans l'image fusionnée (tous les calques visibles)
};
// Retourne faux (et remplit *error) si rien n'a pu être fait ; *warning reçoit un avertissement non bloquant éventuel.
bool removeBackground(Document*, const cv::Mat& rawMask, const RemoveBgParams&, QString* error = nullptr, QString* warning = nullptr);

struct DepthApplyParams {
    DepthRefineParams depth;
    bool makeLayer = true;         // nouveau calque « Carte de profondeur » en niveaux de gris
    bool makeSelection = false;    // sélection par seuil de la carte
    int threshold = 128;
    bool selectBright = true;      // true : zones >= seuil ; false : zones <= seuil
    int feather = 0;
};
bool applyDepth(Document*, const cv::Mat& rawDepth, const DepthApplyParams&, QString* error = nullptr);

struct UpscaleParams {
    bool nativeScale = true;       // sinon : facteur final personnalisé (rééchantillonné après le modèle)
    double finalScale = 2.0;
    long long maxPixels = 64LL * 1000 * 1000;   // garde-fou mémoire sur la taille du résultat
};
// Agrandit TOUT le document (tous les calques et masques). Retourne faux (message dans *error) si impossible.
// `progress(i, n)` est appelé avant chaque calque (peut appeler processEvents) ; retourner faux l'annule proprement.
bool aiUpscale(Document*, const UpscaleParams&, QString* error = nullptr, const std::function<bool(int, int)>& progress = {});

// --- Stable Diffusion (voir src/ai/SdBackend.h) : reçoivent une image DÉJÀ générée, donc testables sans modèle.
// Recolle le résultat d'un inpainting dans le calque actif (seule la zone du plan change) : une étape d'historique.
bool sdApplyInpaint(Document*, const Sd::InpaintPlan&, const cv::Mat& generated, QString* error = nullptr);
// Ajoute l'image générée comme nouveau calque, placée selon `placement`. Retourne le calque (nullptr si impossible).
Layer::Ptr sdAddImage(Document*, const cv::Mat& generated, Sd::Placement placement, const QString& prompt, QString* error = nullptr);
// Nouveau document contenant l'image générée (quand aucun document n'est ouvert).
Document* sdNewDocument(const cv::Mat& generated, const QString& prompt);

QString uniqueLayerName(const Document*, const QString& base);
}
