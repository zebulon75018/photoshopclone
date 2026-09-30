#pragma once
// API des tâches IA exposées à l'application (voir AIModels.h pour la configuration des chemins de modèles).
// Toutes les fonctions sont sûres à appeler même si la bibliothèque vision.cpp n'a pas été compilée
// (PC_HAVE_VISIONCPP non défini) ou si aucun modèle n'est configuré : elles renvoient alors un Result en échec
// avec un message explicite, plutôt que de planter ou de lever une exception.
//
// Toute la charge de calcul (chargement du modèle, inférence) est SYNCHRONE et bloque l'interface le temps du
// calcul (de quelques dizaines de ms à une quinzaine de secondes sur CPU selon le modèle et la taille d'image :
// voir les repères de performance dans depend/visioncpp/UPSTREAM_README.md). Il n'y a pas de fil d'exécution
// séparé dans cette version : c'est une limite connue, documentée dans le README du projet.
#include <opencv2/core.hpp>
#include <QPoint>
#include <QRect>
#include <QString>
#include "AIModels.h"

namespace AIBackend {

struct Result {
    bool ok = false;
    QString error;   // message lisible (fichier introuvable, format invalide, bibliothèque absente...) si !ok
    cv::Mat data;     // BGRA (CV_8UC4) ou masque/carte niveaux de gris (CV_8UC1) selon la fonction ; vide si !ok
    int scale = 1;    // rempli par upscale() : facteur d'agrandissement réellement appliqué par le modèle
};

bool available();   // bibliothèque vision.cpp compilée dans cette build (indépendant de la configuration des modèles)

// BiRefNet — segmentation dichotomique : masque alpha (CV_8UC1, dégradé 0..255, "1" = sujet) de la taille de `bgra`.
Result segmentDichotomous(const cv::Mat& bgra);

// MobileSAM — segmentation par indication. `samEncode` doit être appelé une fois sur l'image avant tout
// `samComputePoint`/`samComputeBox` ; ceux-ci réutilisent l'encodage tant qu'il n'est pas recalculé (rapide, "temps
// réel"). Un nouvel appel à `samEncode` est nécessaire si l'image affichée change (nouveau document, calque actif
// modifié...) : c'est à l'appelant (l'outil) de décider quand ré-encoder, l'encodage n'est jamais invalidé tout seul.
bool samEncode(const cv::Mat& bgra, QString* error = nullptr);
bool samReady();   // vrai si un encodage est disponible pour samCompute*
Result samComputePoint(QPoint p);
Result samComputeBox(QRect box);

// Depth-Anything — carte de profondeur relative normalisée par le modèle sur [0,255] (CV_8UC1). Depth-Anything V2
// produit une profondeur relative *inversée* : les valeurs élevées correspondent en général aux objets les plus proches
// (à confirmer avec votre fichier de modèle : l'interface propose une case « Inverser »).
Result estimateDepth(const cv::Mat& bgra);

// Estimation des couleurs de premier plan aux bords d'un masque (« défrangeage », évite le liseré de l'ancien fond) :
// retourne `bgra` avec RGB corrigé et alpha = `mask` (CV_8UC1, même taille). Ne nécessite AUCUN modèle IA (algorithme
// de vision.cpp seulement) ; `radius` = rayon du flou de fusion en px (défaut de la bibliothèque : 30).
Result estimateForeground(const cv::Mat& bgra, const cv::Mat& mask, int radius = 30);

// Valide un fichier de modèle sans le charger entièrement : existence, format GGUF, et architecture attendue
// (ex. refuser un fichier SAM configuré par erreur comme BiRefNet — ce qui pourrait faire planter la bibliothèque).
// Retourne une chaîne vide si tout est correct, sinon un message d'erreur lisible.
QString validateModelFile(AIArchitecture arch, const QString& path);

// MI-GAN — comble `bgra` là où `mask` (CV_8UC1, non nul = zone à reconstruire) est actif, à partir du voisinage.
// Contrairement à cv::inpaint, la résolution de travail du modèle est fixe (256 ou 512 px) : l'appelant est
// responsable de rogner la zone à traiter (voir effects/Inpaint.cpp) pour préserver le plus de détail possible.
Result inpaint(const cv::Mat& bgra, const cv::Mat& mask);

// Real-ESRGAN — agrandit `bgra` par le facteur du modèle (généralement ×4, parfois ×2). `result.scale` indique
// le facteur réellement utilisé, nécessaire pour redimensionner le reste du document en conséquence.
Result upscale(const cv::Mat& bgra);

// Libère tous les modèles chargés (utile après avoir changé un chemin de modèle dans les réglages).
void clearCache();

}
