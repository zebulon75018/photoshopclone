#include "Effect.h"
#include <opencv2/imgproc.hpp>

EffectRegistry::EffectRegistry() {
    registerFilters(*this);
    registerAdjustments(*this);
    registerRetouch(*this);
}

EffectRegistry& EffectRegistry::instance() {
    static EffectRegistry r;
    return r;
}

EffectPtr EffectRegistry::find(const QString& id) const {
    for (auto& e : m_all) if (e->id == id) return e;
    return nullptr;
}

QStringList EffectRegistry::categories(const QString& prefix) const {
    QStringList out;
    for (auto& e : m_all)
        if (e->id.startsWith(prefix + ".") && !out.contains(e->category)) out << e->category;
    return out;
}

void EffectRegistry::add(const QString& id, const QString& name, const QString& category, std::vector<ParamDef> defs, Effect::Fn fn) {
    auto e = std::make_shared<Effect>();
    e->id = id; e->name = name; e->category = category; e->defs = std::move(defs); e->fn = std::move(fn);
    m_all.push_back(e);
}

void EffectRegistry::addMasked(const QString& id, const QString& name, const QString& category, std::vector<ParamDef> defs,
                               Effect::MaskFn fn, bool requiresSelection, bool supportsSampleAllLayers) {
    auto e = std::make_shared<Effect>();
    e->id = id; e->name = name; e->category = category; e->defs = std::move(defs);
    e->maskFn = std::move(fn);
    e->requiresSelection = requiresSelection;
    e->supportsSampleAllLayers = supportsSampleAllLayers;
    m_all.push_back(e);
}

namespace EffectDiag {
static QString g_error;
void setError(const QString& s) { g_error = s; }
QString takeError() { QString s = g_error; g_error.clear(); return s; }
}

cv::Mat blendWithSelection(const cv::Mat& orig, const cv::Mat& result, const cv::Mat& sel) {
    if (sel.empty()) return result;
    cv::Mat out = orig.clone();
    const int rows = orig.rows, cols = orig.cols;
#pragma omp parallel for
    for (int y = 0; y < rows; ++y) {
        const cv::Vec4b* o = orig.ptr<cv::Vec4b>(y);
        const cv::Vec4b* r = result.ptr<cv::Vec4b>(y);
        const uchar* s = sel.ptr<uchar>(y);
        cv::Vec4b* d = out.ptr<cv::Vec4b>(y);
        for (int x = 0; x < cols; ++x) {
            int a = s[x];
            if (a == 0) continue;
            if (a == 255) { d[x] = r[x]; continue; }
            for (int c = 0; c < 4; ++c) d[x][c] = uchar((o[x][c] * (255 - a) + r[x][c] * a + 127) / 255);
        }
    }
    return out;
}
