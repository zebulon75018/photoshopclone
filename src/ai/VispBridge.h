#pragma once
// Passerelle sans copie (autant que possible) entre cv::Mat (BGRA/gris, convention OpenCV de ce projet) et les
// structures d'image de vision.cpp (visp::image_view / image_data). N'est compilé que si PC_HAVE_VISIONCPP est
// défini (voir CMakeLists.txt) : la bibliothèque vision.cpp doit être présente sous depend/visioncpp/.
#ifdef PC_HAVE_VISIONCPP
#include <opencv2/core.hpp>
#include <visp/image.h>

namespace ai {

// Vue en lecture seule sur `m` (CV_8UC4 BGRA ou CV_8UC1 masque) : AUCUNE copie, respecte le pas de ligne réel de
// `m` (fonctionne donc aussi sur une sous-image/ROI non contiguë). `m` doit rester en vie tant que la vue est utilisée.
visp::image_view viewOf(const cv::Mat& m);

// Copie profonde d'une image_data (n'importe quel format entier 8 bits à 1/3/4 canaux) vers un cv::Mat BGRA (CV_8UC4).
// Les canaux sont réordonnés si besoin (rgba_u8, argb_u8, rgb_u8 -> bgra_u8) ; alpha vaut 255 si le format n'en a pas.
cv::Mat toMatBGRA(const visp::image_data& img);

// Copie profonde d'une image_data mono-canal (alpha_u8 ou alpha_f32, valeurs [0,1] pour ce dernier) vers CV_8UC1.
cv::Mat toMatGray(const visp::image_data& img);

}
#endif
