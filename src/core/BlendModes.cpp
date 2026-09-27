#include "BlendModes.h"
#include <algorithm>
#include <cmath>

QStringList blendModeNames() {
    return {"Normal", "Obscurcir", "Produit", "Densité couleur +", "Densité linéaire +", "Éclaircir",
            "Superposition", "Densité couleur -", "Densité linéaire -", "Incrustation", "Lumière tamisée",
            "Lumière crue", "Différence", "Exclusion", "Teinte", "Saturation", "Couleur", "Luminosité"};
}

namespace {
struct V3 { float r, g, b; };

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float lum(V3 c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
V3 clipColor(V3 c) {
    float l = lum(c), n = std::min({c.r, c.g, c.b}), x = std::max({c.r, c.g, c.b});
    if (n < 0) { c.r = l + (c.r - l) * l / (l - n); c.g = l + (c.g - l) * l / (l - n); c.b = l + (c.b - l) * l / (l - n); }
    if (x > 1) { c.r = l + (c.r - l) * (1 - l) / (x - l); c.g = l + (c.g - l) * (1 - l) / (x - l); c.b = l + (c.b - l) * (1 - l) / (x - l); }
    return c;
}
V3 setLum(V3 c, float l) { float d = l - lum(c); return clipColor({c.r + d, c.g + d, c.b + d}); }
float sat(V3 c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }
V3 setSat(V3 c, float s) {
    float* ch[3] = {&c.r, &c.g, &c.b};
    std::sort(ch, ch + 3, [](float* a, float* b) { return *a < *b; });
    float mn = *ch[0], md = *ch[1], mx = *ch[2];
    if (mx > mn) { *ch[1] = (md - mn) * s / (mx - mn); *ch[2] = s; } else { *ch[1] = 0; *ch[2] = 0; }
    *ch[0] = 0;
    return c;
}

inline float sep(BlendMode m, float b, float s) {
    switch (m) {
    case BlendMode::Darken: return std::min(b, s);
    case BlendMode::Multiply: return b * s;
    case BlendMode::ColorBurn: return s <= 0 ? (b >= 1 ? 1.f : 0.f) : 1 - std::min(1.f, (1 - b) / s);
    case BlendMode::LinearBurn: return std::max(0.f, b + s - 1);
    case BlendMode::Lighten: return std::max(b, s);
    case BlendMode::Screen: return b + s - b * s;
    case BlendMode::ColorDodge: return s >= 1 ? (b <= 0 ? 0.f : 1.f) : std::min(1.f, b / (1 - s));
    case BlendMode::LinearDodge: return std::min(1.f, b + s);
    case BlendMode::Overlay: return sep(BlendMode::HardLight, s, b);
    case BlendMode::HardLight: return s <= 0.5f ? b * 2 * s : (b + (2 * s - 1) - b * (2 * s - 1));
    case BlendMode::SoftLight: {
        if (s <= 0.5f) return b - (1 - 2 * s) * b * (1 - b);
        float d = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
        return b + (2 * s - 1) * (d - b);
    }
    case BlendMode::Difference: return std::fabs(b - s);
    case BlendMode::Exclusion: return b + s - 2 * b * s;
    default: return s;
    }
}

V3 blendColor(BlendMode m, V3 b, V3 s) {
    switch (m) {
    case BlendMode::Hue: return setLum(setSat(s, sat(b)), lum(b));
    case BlendMode::Saturation: return setLum(setSat(b, sat(s)), lum(b));
    case BlendMode::Color: return setLum(s, lum(b));
    case BlendMode::Luminosity: return setLum(b, lum(s));
    default: return {sep(m, b.r, s.r), sep(m, b.g, s.g), sep(m, b.b, s.b)};
    }
}
}

void Blend::over(cv::Mat& dst, const cv::Mat& src, cv::Point origin, cv::Rect region, BlendMode mode, float opacity,
                 const cv::Mat& mask, const cv::Mat& sel) {
    region &= cv::Rect(0, 0, dst.cols, dst.rows);
    region &= cv::Rect(origin.x, origin.y, src.cols, src.rows);
    if (region.empty() || opacity <= 0) return;
    const bool nonSep = mode >= BlendMode::Hue;
    const float inv255 = 1.f / 255.f;
#pragma omp parallel for
    for (int y = region.y; y < region.y + region.height; ++y) {
        cv::Vec4b* d = dst.ptr<cv::Vec4b>(y);
        const cv::Vec4b* s = src.ptr<cv::Vec4b>(y - origin.y);
        const uchar* mk = mask.empty() ? nullptr : mask.ptr<uchar>(y);
        const uchar* sl = sel.empty() ? nullptr : sel.ptr<uchar>(y);
        for (int x = region.x; x < region.x + region.width; ++x) {
            const cv::Vec4b sp = s[x - origin.x];
            if (sp[3] == 0) continue;
            float as = sp[3] * inv255 * opacity;
            if (mk) as *= mk[x] * inv255;
            if (sl) as *= sl[x] * inv255;
            if (as <= 0.f) continue;
            cv::Vec4b& dp = d[x];
            const float ab = dp[3] * inv255;
            if (mode == BlendMode::Normal && (as >= 0.9999f || ab <= 0.f)) {
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = uchar(std::lround(as * 255.f));
                continue;
            }
            const float ar = as + ab * (1 - as);
            if (ar <= 0.f) continue;
            V3 cs{sp[2] * inv255, sp[1] * inv255, sp[0] * inv255};
            V3 cb{dp[2] * inv255, dp[1] * inv255, dp[0] * inv255};
            V3 bl = cs;
            if (mode != BlendMode::Normal && ab > 0.f) {
                if (nonSep) bl = blendColor(mode, cb, cs);
                else bl = {sep(mode, cb.r, cs.r), sep(mode, cb.g, cs.g), sep(mode, cb.b, cs.b)};
            }
            auto mix = [&](float b, float sc, float bc) {
                return clamp01(((1 - as) * ab * b + as * ((1 - ab) * sc + ab * bc)) / ar);
            };
            dp[2] = uchar(std::lround(mix(cb.r, cs.r, bl.r) * 255.f));
            dp[1] = uchar(std::lround(mix(cb.g, cs.g, bl.g) * 255.f));
            dp[0] = uchar(std::lround(mix(cb.b, cs.b, bl.b) * 255.f));
            dp[3] = uchar(std::lround(ar * 255.f));
        }
    }
}
