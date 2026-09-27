#pragma once
#include <opencv2/core.hpp>
#include <QStringList>

enum class BlendMode : int {
    Normal, Darken, Multiply, ColorBurn, LinearBurn, Lighten, Screen, ColorDodge, LinearDodge,
    Overlay, SoftLight, HardLight, Difference, Exclusion, Hue, Saturation, Color, Luminosity, Count
};

QStringList blendModeNames();   // noms affichés (français, comme Photoshop FR)

namespace Blend {
// Compose `src` (BGRA alpha droit, dont le pixel (0,0) est en `origin` dans le repère de dst) sur `dst`.
// `region` est exprimée dans le repère de dst. `layerMask` et `sel` sont des masques 8 bits de la taille de dst.
void over(cv::Mat& dst, const cv::Mat& src, cv::Point origin, cv::Rect region, BlendMode mode = BlendMode::Normal,
          float opacity = 1.f, const cv::Mat& layerMask = cv::Mat(), const cv::Mat& sel = cv::Mat());
}
