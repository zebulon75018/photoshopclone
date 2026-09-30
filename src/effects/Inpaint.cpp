#include "Inpaint.h"
#include "core/MatUtil.h"
#include "ai/AIBackend.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <algorithm>
#include <cmath>

cv::Mat inpaintBGRA(const cv::Mat& src, const cv::Mat& sel, const InpaintParams& p) {
    if (sel.empty() || src.empty()) return src.clone();

    // Masque binaire de la zone à reconstruire (les sélections adoucies ont des bords progressifs : on les prend
    // en compte au-delà d'un seuil bas, l'appelant se charge ensuite d'adoucir la transition avec blendWithSelection).
    cv::Mat bin;
    cv::threshold(sel, bin, 10, 255, cv::THRESH_BINARY);
    if (p.expand > 0) {
        cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * p.expand + 1, 2 * p.expand + 1));
        cv::dilate(bin, bin, k);
    }
    cv::Rect bb = cv::boundingRect(bin);
    if (bb.empty()) return src.clone();

    // Marge de contexte autour de la zone à reconstruire, pour laisser à l'algorithme assez de voisinage à échantillonner.
    const int pad = int(std::ceil(std::max(1.0, p.radius))) + 32;
    cv::Rect region = cv::Rect(bb.x - pad, bb.y - pad, bb.width + 2 * pad, bb.height + 2 * pad) & mu::bounds(src);

    cv::Mat bgr;
    cv::cvtColor(src(region), bgr, cv::COLOR_BGRA2BGR);
    cv::Mat out;
    cv::inpaint(bgr, bin(region), out, std::max(1.0, p.radius), p.algorithm == 0 ? cv::INPAINT_NS : cv::INPAINT_TELEA);

    cv::Mat outBgra;
    cv::cvtColor(out, outBgra, cv::COLOR_BGR2BGRA);
    cv::Mat alphaOrig, alphaOut;
    cv::extractChannel(src(region), alphaOrig, 3);
    cv::max(alphaOrig, bin(region), alphaOut);   // opaque partout où l'on vient de reconstruire
    cv::insertChannel(alphaOut, outBgra, 3);

    cv::Mat result = src.clone();
    outBgra.copyTo(result(region));
    return result;
}

cv::Mat inpaintMiGan(const cv::Mat& src, const cv::Mat& sel, int expand, QString* error) {
    if (sel.empty() || src.empty()) return src.clone();
    cv::Mat bin;
    cv::threshold(sel, bin, 10, 255, cv::THRESH_BINARY);
    if (expand > 0) cv::dilate(bin, bin, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * expand + 1, 2 * expand + 1)));
    cv::Rect bb = cv::boundingRect(bin);
    if (bb.empty()) return src.clone();

    // Carré de contexte autour de la zone, décalé pour rester dans l'image (le modèle déforme un rectangle non carré).
    const int maxSide = std::min(src.cols, src.rows);
    const int side = std::clamp(std::max(bb.width, bb.height) * 2 + 64, std::min(128, maxSide), maxSide);
    const int x0 = std::clamp(bb.x + bb.width / 2 - side / 2, 0, src.cols - side);
    const int y0 = std::clamp(bb.y + bb.height / 2 - side / 2, 0, src.rows - side);
    const cv::Rect crop(x0, y0, side, side);

    auto r = AIBackend::inpaint(src(crop).clone(), bin(crop).clone());
    if (!r.ok || r.data.size() != cv::Size(side, side)) {
        if (error) *error = r.ok ? QString("MI-GAN : taille de sortie inattendue.") : r.error;
        return src.clone();
    }
    cv::Mat result = src.clone();
    r.data.copyTo(result(crop), bin(crop));    // seulement dans la zone à combler ; alpha = 255 (masque)
    return result;
}
