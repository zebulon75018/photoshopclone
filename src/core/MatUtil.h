#pragma once
// Utilitaires de conversion Qt <-> OpenCV. Tous les calques sont en BGRA 8 bits, alpha "droit" (non prémultiplié).
#include <opencv2/core.hpp>
#include <QImage>
#include <QRect>
#include <QColor>

namespace mu {
inline cv::Rect toCv(const QRect& r) { return {r.x(), r.y(), r.width(), r.height()}; }
inline QRect toQt(const cv::Rect& r) { return {r.x, r.y, r.width, r.height}; }
inline cv::Vec4b bgra(const QColor& c) { return {uchar(c.blue()), uchar(c.green()), uchar(c.red()), uchar(c.alpha())}; }
inline cv::Rect bounds(const cv::Mat& m) { return {0, 0, m.cols, m.rows}; }
inline cv::Mat newMat(QSize s, const cv::Vec4b& c = {0, 0, 0, 0}) {
    return cv::Mat(s.height(), s.width(), CV_8UC4, cv::Scalar(c[0], c[1], c[2], c[3]));
}

inline float lum(float r, float g, float b) { return 0.299f * r + 0.587f * g + 0.114f * b; }
QImage toQImage(const cv::Mat& bgra);          // copie profonde (Format_ARGB32)
cv::Mat fromQImage(const QImage& img);         // copie profonde -> BGRA
QImage thumbnail(const cv::Mat& bgra, QSize box, bool checker = true);
void premultiply(cv::Mat& bgra);
void unpremultiply(cv::Mat& bgra);
}
