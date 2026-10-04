#include "SdImaging.h"
#include "MaskRefine.h"
#include "effects/Effect.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(SdImaging) }; }   // traductions hors classes QObject (voir translations/)

namespace Sd {

int roundTo8(double v, int minV) { return std::max(minV, int(std::lround(v / 8.0)) * 8); }

QStringList samplerNames() {
    return {"euler", "euler_a", "heun", "dpm2", "dpm++2s_a", "dpm++2m", "dpm++2mv2", "ipndm", "ipndm_v", "lcm",
            "ddim_trailing", "tcd", "res_multistep", "res_2s", "er_sde", "lms"};
}
QStringList schedulerNames() {
    return {"discrete", "karras", "exponential", "ays", "gits", "sgm_uniform", "simple", "smoothstep", "kl_optimal", "lcm", "bong_tangent", "beta"};
}

static cv::Mat resizeSmart(const cv::Mat& m, cv::Size to, int shrinkInterp = cv::INTER_AREA, int growInterp = cv::INTER_CUBIC) {
    if (m.size() == to) return m;
    cv::Mat out;
    cv::resize(m, out, to, 0, 0, (to.width * to.height < m.cols * m.rows) ? shrinkInterp : growInterp);
    return out;
}

bool planInpaint(const cv::Mat& source, const cv::Mat& selection, const InpaintPlanParams& p, InpaintPlan* out, QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (source.type() != CV_8UC4 || source.empty()) return fail(Tr::tr("Image source invalide (BGRA attendu)."));
    if (selection.type() != CV_8UC1 || selection.size() != source.size()) return fail(Tr::tr("Sélection invalide (taille différente de l'image)."));

    cv::Mat bin;
    cv::threshold(selection, bin, 10, 255, cv::THRESH_BINARY);
    if (p.expand > 0) cv::dilate(bin, bin, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * p.expand + 1, 2 * p.expand + 1)));
    const cv::Rect bb = cv::boundingRect(bin);
    if (bb.empty()) return fail(Tr::tr("La sélection est vide."));

    const int maxSide = std::max(bb.width, bb.height);
    const int wantSide = maxSide + 2 * int(std::lround(maxSide * std::clamp(p.contextPercent, 0, 300) / 100.0));
    // Carré autour de la zone (ou le plus grand rectangle possible si l'image est plus petite), jamais plus petit que la zone.
    const int cw = std::max(std::min(wantSide, source.cols), bb.width);
    const int ch = std::max(std::min(wantSide, source.rows), bb.height);
    const int x0 = std::clamp(bb.x + bb.width / 2 - cw / 2, 0, std::max(0, source.cols - cw));
    const int y0 = std::clamp(bb.y + bb.height / 2 - ch / 2, 0, std::max(0, source.rows - ch));
    const cv::Rect crop(x0, y0, cw, ch);

    const int res = std::clamp(roundTo8(p.workRes, 128), 128, 2048);
    const double scale = double(res) / std::max(cw, ch);
    const cv::Size work(roundTo8(cw * scale), roundTo8(ch * scale));

    cv::Mat cropImg = bleedColors(source(crop).clone());       // évite que l'ancien RGB caché sous alpha 0 (souvent noir) contamine le contexte
    InpaintPlan plan;
    plan.crop = crop;
    plan.region = bb;
    plan.work = work;
    plan.workImage = resizeSmart(cropImg, work);
    cv::resize(bin(crop), plan.workMask, work, 0, 0, cv::INTER_NEAREST);
    cv::threshold(plan.workMask, plan.workMask, 127, 255, cv::THRESH_BINARY);

    plan.blendMask = cv::Mat(source.size(), CV_8UC1, cv::Scalar(0));
    cv::Mat soft = bin(crop).clone();
    if (p.feather > 0) cv::GaussianBlur(soft, soft, cv::Size(0, 0), std::max(0.3, p.feather / 2.0));
    soft.copyTo(plan.blendMask(crop));
    *out = std::move(plan);
    return true;
}

cv::Mat compositeInpaint(const cv::Mat& target, const InpaintPlan& plan, const cv::Mat& generated) {
    CV_Assert(target.type() == CV_8UC4 && generated.type() == CV_8UC4 && generated.size() == plan.work);
    cv::Mat gen = resizeSmart(generated, plan.crop.size());
    cv::Mat full = target.clone();
    cv::Mat roi = full(plan.crop);
    gen.copyTo(roi);
    cv::insertChannel(cv::Mat(plan.crop.size(), CV_8UC1, cv::Scalar(255)), roi, 3);     // le contenu régénéré est opaque
    return blendWithSelection(target, full, plan.blendMask);
}

cv::Mat placeGenerated(const cv::Mat& gen, QSize doc, Placement placement, const cv::Mat& selection) {
    CV_Assert(gen.type() == CV_8UC4 && !gen.empty());
    const cv::Size ds(doc.width(), doc.height());
    cv::Mat out(ds, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    auto blit = [&](const cv::Mat& img, cv::Point topLeft) {       // copie avec rognage aux bords du document
        cv::Rect dst = cv::Rect(topLeft, img.size()) & cv::Rect(0, 0, ds.width, ds.height);
        if (dst.empty()) return;
        img(cv::Rect(dst.x - topLeft.x, dst.y - topLeft.y, dst.width, dst.height)).copyTo(out(dst));
    };
    auto scaled = [&](double s) {
        return resizeSmart(gen, cv::Size(std::max(1, int(std::lround(gen.cols * s))), std::max(1, int(std::lround(gen.rows * s)))));
    };

    cv::Rect box(0, 0, ds.width, ds.height);
    bool useSel = false;
    if (placement == Placement::IntoSelection) {
        cv::Mat bin;
        if (!selection.empty() && selection.type() == CV_8UC1 && selection.size() == ds) {
            cv::threshold(selection, bin, 10, 255, cv::THRESH_BINARY);
            const cv::Rect bb = cv::boundingRect(bin);
            if (!bb.empty()) { box = bb; useSel = true; }
        }
        if (!useSel) placement = Placement::Fit;
    }

    switch (placement) {
    case Placement::Native:
        blit(gen, cv::Point((ds.width - gen.cols) / 2, (ds.height - gen.rows) / 2));
        break;
    case Placement::Fit: case Placement::IntoSelection: {
        const double s = std::min(double(box.width) / gen.cols, double(box.height) / gen.rows);
        cv::Mat g = scaled(s);
        blit(g, cv::Point(box.x + (box.width - g.cols) / 2, box.y + (box.height - g.rows) / 2));
        break;
    }
    case Placement::Cover: {
        const double s = std::max(double(ds.width) / gen.cols, double(ds.height) / gen.rows);
        cv::Mat g = scaled(s);
        blit(g, cv::Point((ds.width - g.cols) / 2, (ds.height - g.rows) / 2));
        break;
    }
    }
    if (useSel) {                                   // découpe selon la forme (et la douceur) de la sélection
        cv::Mat a;
        cv::extractChannel(out, a, 3);
        cv::multiply(a, selection, a, 1.0 / 255.0);
        cv::insertChannel(a, out, 3);
    }
    return out;
}

}
