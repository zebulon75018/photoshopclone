#pragma once
// Remplissage d'après le contenu (retouche par comblement), basé sur cv::inpaint d'OpenCV.
// Ce n'est PAS l'algorithme "Content-Aware Fill" propriétaire de Photoshop (qui utilise du patch-matching avancé) :
// il s'agit des deux algorithmes classiques d'OpenCV, exemplaires-based (Telea) ou basé équations aux dérivées
// partielles (Navier-Stokes), efficaces pour reboucher de petites zones (rayures, poussière, petits objets)
// à partir de leur voisinage immédiat.
#include <opencv2/core.hpp>
#include <QString>

struct InpaintParams {
    // Valeurs alignées sur les constantes d'OpenCV : cv::INPAINT_NS = 0, cv::INPAINT_TELEA = 1.
    int algorithm = 1;     // 0 = Navier-Stokes, 1 = Telea
    double radius = 3.0;   // rayon (px) du voisinage utilisé par l'algorithme pour reconstruire chaque pixel
    int expand = 0;        // agrandit la zone à reconstruire de N px avant de lancer l'algorithme (atténue les halos résiduels au bord de la sélection)
};

// `src` : image BGRA (n'importe quelle taille). `sel` : masque 8 bits de même taille que `src` ; les pixels non nuls
// désignent la zone à reconstruire (vide = aucune sélection = renvoie une copie de `src` sans rien changer).
// Retourne une copie BGRA de `src` où la zone reconstruite est remplie à partir de son voisinage ; les pixels en
// dehors de la zone (agrandie le cas échéant par `expand`) sont identiques à `src` au bit près. Les pixels reconstruits
// sont rendus opaques (alpha = 255) même si `src` y était transparent, puisqu'ils reçoivent une nouvelle couleur.
cv::Mat inpaintBGRA(const cv::Mat& src, const cv::Mat& sel, const InpaintParams& p);

// Variante par réseau de neurones (MI-GAN, via src/ai/AIBackend). Le modèle travaille à résolution FIXE (256 ou 512 px) :
// on lui donne donc un carré rogné autour de la zone (avec du contexte, ~2× sa taille) puis on recolle le résultat.
// Même contrat que inpaintBGRA ; en cas d'échec (modèle absent...) renvoie une copie de `src` et remplit *error.
cv::Mat inpaintMiGan(const cv::Mat& src, const cv::Mat& sel, int expand, QString* error);
