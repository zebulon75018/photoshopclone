#pragma once
#include "Effect.h"
#include "core/MatUtil.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <array>
#include <cmath>

namespace fx {
inline void split(const cv::Mat& src, cv::Mat& bgr, cv::Mat& alpha) {
    cv::cvtColor(src, bgr, cv::COLOR_BGRA2BGR);
    cv::extractChannel(src, alpha, 3);
}
inline cv::Mat merge(const cv::Mat& bgr, const cv::Mat& alpha) {
    cv::Mat out(bgr.size(), CV_8UC4);
    cv::Mat in[] = {bgr, alpha};
    int ft[] = {0, 0, 1, 1, 2, 2, 3, 3};
    cv::mixChannels(in, 2, &out, 1, ft, 4);
    return out;
}
// Applique un traitement sur BGR (3 canaux) en conservant l'alpha.
template <class F> cv::Mat onBGR(const cv::Mat& src, F f) {
    cv::Mat bgr, a;
    split(src, bgr, a);
    return merge(f(bgr), a);
}
// Applique un traitement spatial sur l'image prémultipliée (évite les halos aux bords transparents).
template <class F> cv::Mat spatial(const cv::Mat& src, F f) {
    cv::Mat pm = src.clone();
    mu::premultiply(pm);
    cv::Mat out = f(pm);
    mu::unpremultiply(out);
    return out;
}
template <class F> cv::Mat warp(const cv::Mat& s, F f) {
    cv::Mat mx(s.size(), CV_32F), my(s.size(), CV_32F);
#pragma omp parallel for
    for (int y = 0; y < s.rows; ++y)
        for (int x = 0; x < s.cols; ++x) {
            float sx = float(x), sy = float(y);
            f(x, y, sx, sy);
            mx.at<float>(y, x) = sx;
            my.at<float>(y, x) = sy;
        }
    cv::Mat pm = s.clone(), out;
    mu::premultiply(pm);
    cv::remap(pm, out, mx, my, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
    mu::unpremultiply(out);
    return out;
}
// LUT 4 canaux (B,G,R identiques ou distincts, alpha inchangé).
inline cv::Mat applyLut(const cv::Mat& src, const std::array<uchar, 256>& b, const std::array<uchar, 256>& g, const std::array<uchar, 256>& r) {
    cv::Mat lut(1, 256, CV_8UC4);
    for (int i = 0; i < 256; ++i) lut.at<cv::Vec4b>(0, i) = cv::Vec4b(b[i], g[i], r[i], uchar(i));
    cv::Mat out;
    cv::LUT(src, lut, out);
    return out;
}
inline cv::Mat applyLut(const cv::Mat& src, const std::array<uchar, 256>& l) { return applyLut(src, l, l, l); }
inline uchar clampU8(double v) { return uchar(v < 0 ? 0 : (v > 255 ? 255 : std::lround(v))); }
}
