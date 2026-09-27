#pragma once
// Fonctions utilitaires sur les masques de sélection (CV_8UC1, 255 = sélectionné, tableau vide = aucune sélection).
#include <opencv2/core.hpp>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <vector>

namespace Sel {
enum class Mode { Replace, Add, Subtract, Intersect };

cv::Mat fromRect(QSize size, const QRectF& r);
cv::Mat fromEllipse(QSize size, const QRectF& r);
cv::Mat fromPolygon(QSize size, const QPolygonF& poly);
cv::Mat magicWand(const cv::Mat& bgra, cv::Point seed, int tolerance, bool contiguous, bool antiAlias);

cv::Mat combine(const cv::Mat& current, const cv::Mat& add, Mode mode, QSize size);
cv::Mat invert(const cv::Mat& sel, QSize size);
cv::Mat feather(const cv::Mat& sel, double radius);
cv::Mat grow(const cv::Mat& sel, int px);      // px<0 : contracter
cv::Mat smooth(const cv::Mat& sel, int radius);
cv::Mat translated(const cv::Mat& sel, double dx, double dy);

bool isEmpty(const cv::Mat& sel);
cv::Rect bounds(const cv::Mat& sel);
std::vector<std::vector<cv::Point>> contours(const cv::Mat& sel);   // pour les "fourmis marchantes"
}
