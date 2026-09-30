#include "MaskRefine.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

cv::Mat refineMask(const cv::Mat& raw, const MaskRefineParams& p) {
    CV_Assert(raw.type() == CV_8UC1);
    cv::Mat m = raw.clone();
    if (p.threshold > 0) cv::threshold(m, m, p.threshold - 1, 255, cv::THRESH_BINARY);

    if (p.keepLargest) {
        cv::Mat bin, labels, stats, centroids;
        cv::threshold(m, bin, 127, 255, cv::THRESH_BINARY);
        int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
        if (n > 2) {   // 0 = fond ; au moins deux composantes de sujet
            int best = 1;
            for (int i = 2; i < n; ++i) if (stats.at<int>(i, cv::CC_STAT_AREA) > stats.at<int>(best, cv::CC_STAT_AREA)) best = i;
            cv::Mat keep = (labels == best);
            cv::dilate(keep, keep, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));   // conserve les bords doux voisins
            cv::Mat out = cv::Mat::zeros(m.size(), CV_8UC1);
            m.copyTo(out, keep);
            m = out;
        }
    }

    if (p.shift != 0) {
        int r = std::abs(p.shift);
        cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * r + 1, 2 * r + 1));
        if (p.shift > 0) cv::dilate(m, m, k); else cv::erode(m, m, k);
    }
    if (p.feather > 0) cv::GaussianBlur(m, m, cv::Size(0, 0), std::max(0.3, p.feather / 2.0));
    if (p.invert) cv::bitwise_not(m, m);
    return m;
}

cv::Mat applyMaskToAlpha(const cv::Mat& bgra, const cv::Mat& mask) {
    CV_Assert(bgra.type() == CV_8UC4 && mask.type() == CV_8UC1 && bgra.size() == mask.size());
    cv::Mat out = bgra.clone(), a;
    cv::extractChannel(out, a, 3);
    cv::multiply(a, mask, a, 1.0 / 255.0);
    cv::insertChannel(a, out, 3);
    return out;
}

cv::Mat bleedColors(const cv::Mat& bgra) {
    CV_Assert(bgra.type() == CV_8UC4);
    cv::Mat alpha8;
    cv::extractChannel(bgra, alpha8, 3);
    if (cv::countNonZero(alpha8 < 255) == 0) return bgra;

    cv::Mat bgr, a;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    bgr.convertTo(bgr, CV_32FC3);
    alpha8.convertTo(a, CV_32F, 1.0 / 255.0);
    cv::Mat a3;
    cv::cvtColor(a, a3, cv::COLOR_GRAY2BGR);
    cv::Mat premult = bgr.mul(a3);

    cv::Mat est = cv::Mat::zeros(bgr.size(), CV_32FC3);
    cv::Mat have = cv::Mat::zeros(bgr.size(), CV_8UC1);
    for (double sigma : {1.5, 4.0, 12.0, 40.0, 120.0}) {
        cv::Mat bw, bc;
        cv::GaussianBlur(a, bw, cv::Size(0, 0), sigma);
        cv::GaussianBlur(premult, bc, cv::Size(0, 0), sigma);
        cv::Mat fresh = (bw > 0.02f) & (have == 0);
        cv::Mat bw3, ratio;
        cv::cvtColor(bw, bw3, cv::COLOR_GRAY2BGR);
        cv::divide(bc, bw3 + 1e-6f, ratio);
        ratio.copyTo(est, fresh);
        have |= fresh;
    }
    if (cv::countNonZero(have == 0) > 0) {   // aucun pixel opaque du tout dans le voisinage : couleur moyenne globale
        cv::Scalar mean = cv::mean(bgr, alpha8 > 0);
        est.setTo(mean, have == 0);
    }
    // Là où alpha est faible on prend l'estimation ; là où il est fort, la vraie couleur.
    cv::Mat out3 = bgr.mul(a3) + est.mul(cv::Scalar::all(1.0) - a3);
    cv::Mat out8;
    out3.convertTo(out8, CV_8UC3);
    cv::Mat res;
    cv::cvtColor(out8, res, cv::COLOR_BGR2BGRA);
    cv::insertChannel(alpha8, res, 3);
    return res;
}

cv::Mat refineDepth(const cv::Mat& raw, const DepthRefineParams& p) {
    CV_Assert(raw.type() == CV_8UC1);
    cv::Mat d = raw.clone();
    if (p.autoLevels) {
        int hist[256] = {0};
        for (int y = 0; y < d.rows; ++y) for (int x = 0; x < d.cols; ++x) hist[d.at<uchar>(y, x)]++;
        double total = double(d.rows) * d.cols, acc = 0;
        int lo = 0, hi = 255;
        for (int i = 0; i < 256; ++i) { acc += hist[i]; if (acc >= 0.01 * total) { lo = i; break; } }
        acc = 0;
        for (int i = 255; i >= 0; --i) { acc += hist[i]; if (acc >= 0.01 * total) { hi = i; break; } }
        if (hi > lo) d.convertTo(d, CV_8UC1, 255.0 / (hi - lo), -255.0 * lo / (hi - lo));
    }
    if (p.smooth > 0) cv::GaussianBlur(d, d, cv::Size(0, 0), p.smooth);
    if (p.invert) cv::bitwise_not(d, d);
    return d;
}

cv::Mat depthToSelection(const cv::Mat& depth, int threshold, bool bright, int feather) {
    CV_Assert(depth.type() == CV_8UC1);
    cv::Mat sel;
    cv::threshold(depth, sel, bright ? threshold - 1 : threshold, 255, bright ? cv::THRESH_BINARY : cv::THRESH_BINARY_INV);
    if (feather > 0) cv::GaussianBlur(sel, sel, cv::Size(0, 0), std::max(0.3, feather / 2.0));
    return cv::countNonZero(sel) == 0 ? cv::Mat() : sel;
}
