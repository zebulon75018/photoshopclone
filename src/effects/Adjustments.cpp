#include "EffectUtil.h"
#include <algorithm>

using fx::onBGR; using fx::clampU8;
using Lut = std::array<uchar, 256>;

namespace {
Lut identity() { Lut l; for (int i = 0; i < 256; ++i) l[i] = uchar(i); return l; }

// Courbe monotone cubique (Fritsch-Carlson) passant par les points de contrôle ([0,1]²) -> LUT 256 entrées.
}
std::array<uchar, 256> curveLut(QPolygonF pts) {
    if (pts.size() < 2) return identity();
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    if (pts.first().x() > 0) pts.prepend(QPointF(0, pts.first().y()));
    if (pts.last().x() < 1) pts.append(QPointF(1, pts.last().y()));
    const int n = pts.size();
    std::vector<double> x(n), y(n), d(n - 1), m(n);
    for (int i = 0; i < n; ++i) { x[i] = pts[i].x(); y[i] = pts[i].y(); }
    for (int i = 0; i < n - 1; ++i) d[i] = (x[i + 1] - x[i]) > 1e-9 ? (y[i + 1] - y[i]) / (x[i + 1] - x[i]) : 0;
    m[0] = d[0]; m[n - 1] = d[n - 2];
    for (int i = 1; i < n - 1; ++i) m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
    for (int i = 0; i < n - 1; ++i) {
        if (d[i] == 0) { m[i] = m[i + 1] = 0; continue; }
        double a = m[i] / d[i], b = m[i + 1] / d[i], h = a * a + b * b;
        if (h > 9) { double t = 3 / std::sqrt(h); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
    }
    Lut lut;
    int k = 0;
    for (int v = 0; v < 256; ++v) {
        double t = v / 255.0;
        while (k < n - 2 && t > x[k + 1]) ++k;
        double h = x[k + 1] - x[k], s = h > 1e-9 ? (t - x[k]) / h : 0;
        double h00 = (1 + 2 * s) * (1 - s) * (1 - s), h10 = s * (1 - s) * (1 - s), h01 = s * s * (3 - 2 * s), h11 = s * s * (s - 1);
        lut[v] = clampU8((h00 * y[k] + h10 * h * m[k] + h01 * y[k + 1] + h11 * h * m[k + 1]) * 255.0);
    }
    return lut;
}
namespace {

void stretchPoints(const cv::Mat& ch, double clip, int& lo, int& hi) {
    int hist[256] = {0};
    for (int y = 0; y < ch.rows; ++y) for (int x = 0; x < ch.cols; ++x) hist[ch.at<uchar>(y, x)]++;
    double total = double(ch.rows) * ch.cols, acc = 0;
    lo = 0; hi = 255;
    for (int i = 0; i < 256; ++i) { acc += hist[i]; if (acc >= clip * total) { lo = i; break; } }
    acc = 0;
    for (int i = 255; i >= 0; --i) { acc += hist[i]; if (acc >= clip * total) { hi = i; break; } }
    if (hi <= lo) { lo = 0; hi = 255; }
}

Lut stretchLut(int lo, int hi) {
    Lut l;
    for (int i = 0; i < 256; ++i) l[i] = clampU8((i - lo) * 255.0 / std::max(1, hi - lo));
    return l;
}

// Conversion HLS flottante (H 0..360, L 0..1, S 0..1) : applique f(h,l,s) à chaque pixel.
template <class F> cv::Mat viaHLS(const cv::Mat& src, F f) {
    return onBGR(src, [&](const cv::Mat& m) {
        cv::Mat fl, hls, o;
        m.convertTo(fl, CV_32F, 1.0 / 255.0);
        cv::cvtColor(fl, hls, cv::COLOR_BGR2HLS);
#pragma omp parallel for
        for (int y = 0; y < hls.rows; ++y) {
            cv::Vec3f* p = hls.ptr<cv::Vec3f>(y);
            for (int x = 0; x < hls.cols; ++x) f(p[x][0], p[x][1], p[x][2]);
        }
        cv::cvtColor(hls, fl, cv::COLOR_HLS2BGR);
        fl.convertTo(o, CV_8U, 255.0);
        return o;
    });
}
inline float cl(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
}

void registerAdjustments(EffectRegistry& R) {
    R.add("adjust.brightness", "Luminosité/Contraste…", "Réglages", {P::Int("b", "Luminosité", -100, 100, 0), P::Int("c", "Contraste", -100, 100, 0)},
          [](const cv::Mat& s, const Params& p) {
              double c = p.d("c") * 2.55, f = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c)), b = p.d("b") * 2.55;
              Lut l; for (int i = 0; i < 256; ++i) l[i] = clampU8(f * (i - 128) + 128 + b);
              return fx::applyLut(s, l);
          });

    R.add("adjust.levels", "Niveaux…", "Réglages",
          {P::Choice("ch", "Couche", {"RVB", "Rouge", "Vert", "Bleu"}), P::Int("ib", "Entrée : noir", 0, 254, 0), P::Dbl("g", "Gamma", 0.1, 9.99, 1.0, 2),
           P::Int("iw", "Entrée : blanc", 1, 255, 255), P::Int("ob", "Sortie : noir", 0, 255, 0), P::Int("ow", "Sortie : blanc", 0, 255, 255)},
          [](const cv::Mat& s, const Params& p) {
              Lut l, id = identity();
              double ib = p.d("ib"), iw = std::max(ib + 1, p.d("iw")), ob = p.d("ob"), ow = p.d("ow"), ig = 1.0 / p.d("g");
              for (int i = 0; i < 256; ++i) {
                  double t = std::clamp((i - ib) / (iw - ib), 0.0, 1.0);
                  l[i] = clampU8(ob + std::pow(t, ig) * (ow - ob));
              }
              int ch = p.i("ch");
              return fx::applyLut(s, ch == 0 || ch == 3 ? l : id, ch == 0 || ch == 2 ? l : id, ch == 0 || ch == 1 ? l : id);
          });

    R.add("adjust.curves", "Courbes…", "Réglages", {P::Curve("rgb", "RVB"), P::Curve("r", "Rouge"), P::Curve("g", "Vert"), P::Curve("b", "Bleu")},
          [](const cv::Mat& s, const Params& p) {
              Lut m = curveLut(p.curve("rgb")), r = curveLut(p.curve("r")), g = curveLut(p.curve("g")), b = curveLut(p.curve("b"));
              Lut R2, G2, B2;
              for (int i = 0; i < 256; ++i) { R2[i] = r[m[i]]; G2[i] = g[m[i]]; B2[i] = b[m[i]]; }
              return fx::applyLut(s, B2, G2, R2);
          });

    R.add("adjust.exposure", "Exposition…", "Réglages", {P::Dbl("ev", "Exposition (IL)", -5, 5, 0, 2), P::Dbl("off", "Décalage", -0.5, 0.5, 0, 3), P::Dbl("g", "Gamma", 0.1, 5, 1, 2)},
          [](const cv::Mat& s, const Params& p) {
              Lut l; double k = std::pow(2.0, p.d("ev"));
              for (int i = 0; i < 256; ++i) l[i] = clampU8(std::pow(std::clamp(i / 255.0 * k + p.d("off"), 0.0, 1.0), 1.0 / p.d("g")) * 255.0);
              return fx::applyLut(s, l);
          });

    R.add("adjust.huesat", "Teinte/Saturation…", "Réglages", {P::Int("h", "Teinte", -180, 180, 0), P::Int("s", "Saturation", -100, 100, 0), P::Int("l", "Luminosité", -100, 100, 0)},
          [](const cv::Mat& s, const Params& p) {
              float dh = float(p.d("h")), ds = float(p.d("s") / 100.0), dl = float(p.d("l") / 100.0);
              return viaHLS(s, [=](float& h, float& l, float& sa) {
                  h = std::fmod(h + dh + 720.f, 360.f);
                  sa = cl(sa * (1 + ds));
                  l = dl > 0 ? l + (1 - l) * dl : l * (1 + dl);
              });
          });

    R.add("adjust.vibrance", "Vibrance…", "Réglages", {P::Int("v", "Vibrance", -100, 100, 30), P::Int("s", "Saturation", -100, 100, 0)},
          [](const cv::Mat& s, const Params& p) {
              float v = float(p.d("v") / 100.0), ds = float(p.d("s") / 100.0);
              return viaHLS(s, [=](float&, float&, float& sa) { sa = cl(sa * (1 + ds) + v * 2 * sa * (1 - sa)); });
          });

    R.add("adjust.colorbalance", "Balance des couleurs…", "Réglages",
          {P::Int("sr", "Ombres : Cyan ↔ Rouge", -100, 100, 0), P::Int("sg", "Ombres : Magenta ↔ Vert", -100, 100, 0), P::Int("sb", "Ombres : Jaune ↔ Bleu", -100, 100, 0),
           P::Int("mr", "Tons moyens : Cyan ↔ Rouge", -100, 100, 0), P::Int("mg", "Tons moyens : Magenta ↔ Vert", -100, 100, 0), P::Int("mb", "Tons moyens : Jaune ↔ Bleu", -100, 100, 0),
           P::Int("hr", "Hautes lumières : Cyan ↔ Rouge", -100, 100, 0), P::Int("hg", "Hautes lumières : Magenta ↔ Vert", -100, 100, 0), P::Int("hb", "Hautes lumières : Jaune ↔ Bleu", -100, 100, 0),
           P::Bool("keep", "Conserver la luminosité", true)},
          [](const cv::Mat& s, const Params& p) {
              double sh[3] = {p.d("sr"), p.d("sg"), p.d("sb")}, mi[3] = {p.d("mr"), p.d("mg"), p.d("mb")}, hi[3] = {p.d("hr"), p.d("hg"), p.d("hb")};
              bool keep = p.b("keep");
              cv::Mat out = s.clone();
#pragma omp parallel for
              for (int y = 0; y < out.rows; ++y) {
                  cv::Vec4b* o = out.ptr<cv::Vec4b>(y);
                  for (int x = 0; x < out.cols; ++x) {
                      double r = o[x][2], g = o[x][1], b = o[x][0], L = mu::lum(float(r), float(g), float(b)) / 255.0;
                      double ws = std::clamp(1 - 2 * L, 0.0, 1.0), wh = std::clamp(2 * L - 1, 0.0, 1.0), wm = 1 - ws - wh;
                      double d[3];
                      for (int c = 0; c < 3; ++c) d[c] = (ws * sh[c] + wm * mi[c] + wh * hi[c]) * 0.6;
                      double nr = r + d[0], ng = g + d[1], nb = b + d[2];
                      if (keep) { double dl = mu::lum(float(r), float(g), float(b)) - mu::lum(float(nr), float(ng), float(nb)); nr += dl; ng += dl; nb += dl; }
                      o[x][2] = clampU8(nr); o[x][1] = clampU8(ng); o[x][0] = clampU8(nb);
                  }
              }
              return out;
          });

    R.add("adjust.bw", "Noir et blanc…", "Réglages", {P::Int("r", "Rouges (%)", -200, 300, 30), P::Int("g", "Verts (%)", -200, 300, 59), P::Int("b", "Bleus (%)", -200, 300, 11)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat g, o;
                  cv::Matx13f k(float(p.d("b") / 100), float(p.d("g") / 100), float(p.d("r") / 100));
                  cv::transform(m, g, k);
                  cv::cvtColor(g, o, cv::COLOR_GRAY2BGR);
                  return o;
              });
          });

    R.add("adjust.photofilter", "Filtre photo…", "Réglages",
          {P::Choice("preset", "Filtre", {"Réchauffant (85)", "Refroidissant (80)", "Sépia", "Personnalisé"}, 0), P::Color("color", "Couleur (si personnalisé)", QColor(255, 140, 0)),
           P::Int("density", "Densité (%)", 0, 100, 25), P::Bool("keep", "Conserver la luminosité", true)},
          [](const cv::Mat& s, const Params& p) {
              static const QColor presets[] = {QColor(236, 138, 0), QColor(0, 109, 255), QColor(172, 122, 51)};
              QColor c = p.i("preset") < 3 ? presets[p.i("preset")] : p.color("color");
              double dens = p.d("density") / 100.0; bool keep = p.b("keep");
              double cr = c.red() / 255.0, cg = c.green() / 255.0, cb = c.blue() / 255.0;
              cv::Mat out = s.clone();
#pragma omp parallel for
              for (int y = 0; y < out.rows; ++y) {
                  cv::Vec4b* o = out.ptr<cv::Vec4b>(y);
                  for (int x = 0; x < out.cols; ++x) {
                      double r = o[x][2], g = o[x][1], b = o[x][0];
                      double fr = r * cr, fg = g * cg, fb = b * cb;
                      if (keep) { double k = mu::lum(float(r), float(g), float(b)) / std::max(1.0, double(mu::lum(float(fr), float(fg), float(fb)))); fr *= k; fg *= k; fb *= k; }
                      o[x][2] = clampU8(r + (fr - r) * dens); o[x][1] = clampU8(g + (fg - g) * dens); o[x][0] = clampU8(b + (fb - b) * dens);
                  }
              }
              return out;
          });

    R.add("adjust.shadowhl", "Ombres/Hautes lumières…", "Réglages", {P::Int("sh", "Ombres (%)", 0, 100, 35), P::Int("hl", "Hautes lumières (%)", 0, 100, 0), P::Int("r", "Rayon (px)", 1, 200, 30)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat g, lb, f, o; cv::cvtColor(m, g, cv::COLOR_BGR2GRAY);
                  g.convertTo(g, CV_32F, 1.0 / 255.0);
                  cv::GaussianBlur(g, lb, cv::Size(0, 0), p.d("r"));
                  m.convertTo(f, CV_32FC3);
                  double sh = p.d("sh") / 100.0, hl = p.d("hl") / 100.0;
#pragma omp parallel for
                  for (int y = 0; y < f.rows; ++y) {
                      cv::Vec3f* fp = f.ptr<cv::Vec3f>(y); const float* l = lb.ptr<float>(y);
                      for (int x = 0; x < f.cols; ++x) {
                          float dark = (1 - l[x]) * (1 - l[x]), bright = l[x] * l[x];
                          for (int c = 0; c < 3; ++c) fp[x][c] = float(fp[x][c] + (255 - fp[x][c]) * sh * dark - fp[x][c] * hl * bright);
                      }
                  }
                  f.convertTo(o, CV_8UC3);
                  return o;
              });
          });

    R.add("adjust.invert", "Négatif", "Réglages", {}, [](const cv::Mat& s, const Params&) { return onBGR(s, [](const cv::Mat& m) { cv::Mat o; cv::bitwise_not(m, o); return o; }); });
    R.add("adjust.desaturate", "Désaturation", "Réglages", {},
          [](const cv::Mat& s, const Params&) { return onBGR(s, [](const cv::Mat& m) { cv::Mat g, o; cv::cvtColor(m, g, cv::COLOR_BGR2GRAY); cv::cvtColor(g, o, cv::COLOR_GRAY2BGR); return o; }); });
    R.add("adjust.threshold", "Seuil…", "Réglages", {P::Int("t", "Niveau de seuil", 1, 255, 128)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) { cv::Mat g, o; cv::cvtColor(m, g, cv::COLOR_BGR2GRAY); cv::threshold(g, g, p.d("t") - 1, 255, cv::THRESH_BINARY); cv::cvtColor(g, o, cv::COLOR_GRAY2BGR); return o; });
          });
    R.add("adjust.posterize", "Isohélie…", "Réglages", {P::Int("n", "Niveaux", 2, 64, 4)},
          [](const cv::Mat& s, const Params& p) {
              Lut l; int n = p.i("n");
              for (int i = 0; i < 256; ++i) l[i] = clampU8(std::floor(i * n / 256.0) * 255.0 / (n - 1));
              return fx::applyLut(s, l);
          });

    // --- Corrections automatiques
    R.add("adjust.autotone", "Niveaux automatiques", "Auto", {},
          [](const cv::Mat& s, const Params&) {
              cv::Mat bgr, a, ch[3]; fx::split(s, bgr, a); cv::split(bgr, ch);
              Lut L[3];
              for (int c = 0; c < 3; ++c) { int lo, hi; stretchPoints(ch[c], 0.005, lo, hi); L[c] = stretchLut(lo, hi); }
              return fx::applyLut(s, L[0], L[1], L[2]);
          });
    R.add("adjust.autocontrast", "Contraste automatique", "Auto", {},
          [](const cv::Mat& s, const Params&) {
              cv::Mat bgr, a, g; fx::split(s, bgr, a); cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
              int lo, hi; stretchPoints(g, 0.005, lo, hi);
              return fx::applyLut(s, stretchLut(lo, hi));
          });
    R.add("adjust.autocolor", "Couleur automatique", "Auto", {},
          [](const cv::Mat& s, const Params&) {   // balance des gris + étirement
              cv::Mat bgr, a; fx::split(s, bgr, a);
              cv::Scalar m = cv::mean(bgr); double g = (m[0] + m[1] + m[2]) / 3.0;
              cv::Mat f, o; bgr.convertTo(f, CV_32FC3, 1.0, 0.0);
              std::vector<cv::Mat> ch; cv::split(f, ch);
              for (int c = 0; c < 3; ++c) ch[c] *= g / std::max(1.0, m[c]);
              cv::merge(ch, f); f.convertTo(o, CV_8UC3);
              cv::Mat res = fx::merge(o, a), oc[3]; cv::split(o, oc);
              Lut L[3];
              for (int c = 0; c < 3; ++c) { int lo, hi; stretchPoints(oc[c], 0.005, lo, hi); L[c] = stretchLut(lo, hi); }
              return fx::applyLut(res, L[0], L[1], L[2]);
          });
}
