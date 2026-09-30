#pragma once
// Post-traitements PURS (OpenCV seulement, aucune dépendance à vision.cpp ni à l'interface) appliqués aux sorties des
// modèles IA : affinage d'un masque de détourage, retouche d'une carte de profondeur, propagation des couleurs vers les
// zones transparentes. Séparés du reste pour être rapides (aperçu en direct sans relancer le réseau) et testables sans
// aucun fichier de modèle.
#include <opencv2/core.hpp>

struct MaskRefineParams {
    int threshold = 0;         // 0 = conserver le masque doux ; 1..255 = seuil dur (>= seuil -> 255, sinon 0)
    bool keepLargest = false;  // ne garder que la plus grande zone connexe (élimine les îlots parasites)
    int shift = 0;             // < 0 : contracter, > 0 : dilater (px), après seuil
    int feather = 0;           // adoucissement final : rayon du flou gaussien (px)
    bool invert = false;       // garder l'arrière-plan plutôt que le sujet
};

// `raw` : masque CV_8UC1 (255 = sujet). Retourne un masque CV_8UC1 de même taille.
cv::Mat refineMask(const cv::Mat& raw, const MaskRefineParams& p);

// Alpha de `bgra` multiplié par `mask` (CV_8UC1 même taille) : image « détourée » (RGB inchangé).
cv::Mat applyMaskToAlpha(const cv::Mat& bgra, const cv::Mat& mask);

// Remplace la couleur des pixels (semi-)transparents par une moyenne pondérée de leur voisinage opaque (multi-échelle).
// Évite les liserés sombres/clairs quand une image à fond transparent est agrandie ou rééchantillonnée (le RGB « caché »
// sous alpha = 0 vaut souvent 0). L'alpha n'est pas modifié. Image déjà entièrement opaque : renvoyée telle quelle.
cv::Mat bleedColors(const cv::Mat& bgra);

struct DepthRefineParams {
    bool invert = false;       // inverser la carte (proche <-> loin)
    bool autoLevels = true;    // étire le contraste entre les percentiles 1 % et 99 %
    int smooth = 0;            // flou gaussien (rayon px) pour lisser les artefacts de la carte
};

// `raw` : carte CV_8UC1 renvoyée par le modèle. Retourne une carte CV_8UC1.
cv::Mat refineDepth(const cv::Mat& raw, const DepthRefineParams& p);

// Sélection issue d'une carte de profondeur : 255 là où carte >= seuil (bright = true) ou <= seuil (bright = false).
cv::Mat depthToSelection(const cv::Mat& depth, int threshold, bool bright, int feather);
