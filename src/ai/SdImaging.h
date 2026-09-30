#pragma once
// Préparation et raccord des images pour Stable Diffusion — fonctions PURES (OpenCV seulement), testables sans modèle.
#include <QSize>
#include <QString>
#include <QStringList>
#include <opencv2/core.hpp>

namespace Sd {

// --- Inpainting sur une sélection --------------------------------------------------------------------------------------------
// Stable Diffusion travaille à taille fixe (typiquement 512 px pour SD 1.x, 1024 px pour SDXL). On ne lui envoie donc pas tout le
// document : on recadre un carré autour de la zone à régénérer (avec une marge de contexte), on le met à l'échelle de travail, puis
// on recolle le résultat en ne touchant QUE la zone sélectionnée (raccord adouci) : le reste de l'image reste identique au bit près.
struct InpaintPlanParams {
    int workRes = 512;        // côté long de l'image de travail (arrondi au multiple de 8 ; 128..2048)
    int contextPercent = 50;  // marge de contexte autour de la zone, en % de sa plus grande dimension (0..300)
    int expand = 4;           // agrandit la zone à régénérer de N px (évite un liseré de l'ancien contenu)
    int feather = 6;          // adoucissement du raccord, en px
};

struct InpaintPlan {
    cv::Rect crop;            // zone du document envoyée au modèle
    cv::Rect region;          // boîte englobante de la zone à régénérer (agrandie), en coordonnées du document
    cv::Size work;            // taille de l'image de travail (multiples de 8)
    cv::Mat workImage;        // BGRA, work : contenu d'origine (couleurs propagées sous les zones transparentes)
    cv::Mat workMask;         // CV_8UC1 binaire (0 / 255), work : 255 = à régénérer
    cv::Mat blendMask;        // CV_8UC1, taille du document : opacité du raccord (0 hors de la zone)
};

// `source` : BGRA (image fusionnée ou calque), `selection` : CV_8UC1 même taille (non nul = à régénérer, seuil 10).
bool planInpaint(const cv::Mat& source, const cv::Mat& selection, const InpaintPlanParams& p, InpaintPlan* out, QString* error = nullptr);

// Recolle `generated` (BGRA, taille `plan.work`) dans `target` (BGRA pleine taille) : seuls les pixels sous `plan.blendMask` changent,
// et deviennent opaques (le contenu régénéré remplit aussi d'éventuels trous transparents).
cv::Mat compositeInpaint(const cv::Mat& target, const InpaintPlan& plan, const cv::Mat& generated);

// --- Placement d'une image générée dans le document -----------------------------------------------------------------------------
enum class Placement {
    Native,        // taille d'origine, centrée (rognée si plus grande que le document)
    Fit,           // mise à l'échelle pour tenir en entier dans le document (proportions conservées)
    Cover,         // mise à l'échelle pour couvrir tout le document (proportions conservées, rognée)
    IntoSelection  // tient dans la boîte de la sélection, découpée selon la sélection (repli : Fit s'il n'y a pas de sélection)
};
// Retourne un calque BGRA de la taille du document (fond transparent).
cv::Mat placeGenerated(const cv::Mat& generated, QSize docSize, Placement placement, const cv::Mat& selection = cv::Mat());

// Noms acceptés par la bibliothèque (str_to_sample_method / str_to_scheduler).
QStringList samplerNames();
QStringList schedulerNames();

// Arrondit au multiple de 8 le plus proche (au moins `minV`).
int roundTo8(double v, int minV = 64);

}
