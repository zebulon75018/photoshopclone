#include "EffectUtil.h"
#include <QStringList>

using fx::spatial; using fx::onBGR; using fx::warp;

void registerFilters(EffectRegistry& R) {
    // ---------------------------------------------------------------- Flou
    R.add("filter.gaussian", "Flou gaussien…", "Atténuation", {P::Dbl("r", "Rayon (px)", 0.1, 250, 4, 1)},
          [](const cv::Mat& s, const Params& p) {
              return spatial(s, [&](const cv::Mat& m) { cv::Mat o; cv::GaussianBlur(m, o, cv::Size(0, 0), p.d("r")); return o; });
          });
    R.add("filter.box", "Flou moyen…", "Atténuation", {P::Int("r", "Rayon (px)", 1, 200, 5)},
          [](const cv::Mat& s, const Params& p) {
              return spatial(s, [&](const cv::Mat& m) { cv::Mat o; int k = 2 * p.i("r") + 1; cv::blur(m, o, cv::Size(k, k)); return o; });
          });
    R.add("filter.motion", "Flou de mouvement…", "Atténuation", {P::Int("angle", "Angle (°)", -90, 90, 0), P::Int("dist", "Distance (px)", 1, 500, 20)},
          [](const cv::Mat& s, const Params& p) {
              int L = p.i("dist"), ks = (L | 1) + 2;
              cv::Mat k = cv::Mat::zeros(ks, ks, CV_32F);
              double a = p.d("angle") * CV_PI / 180, c = (ks - 1) / 2.0, h = (L - 1) / 2.0;
              cv::line(k, cv::Point(int(std::lround(c - std::cos(a) * h)), int(std::lround(c + std::sin(a) * h))),
                       cv::Point(int(std::lround(c + std::cos(a) * h)), int(std::lround(c - std::sin(a) * h))), cv::Scalar(1), 1, cv::LINE_8);
              k /= std::max(1e-6, cv::sum(k)[0]);
              return spatial(s, [&](const cv::Mat& m) { cv::Mat o; cv::filter2D(m, o, -1, k, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE); return o; });
          });
    R.add("filter.surface", "Flou de surface…", "Atténuation", {P::Int("r", "Rayon (px)", 1, 25, 5), P::Int("t", "Seuil", 1, 255, 30)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) { cv::Mat o; cv::bilateralFilter(m, o, std::min(2 * p.i("r") + 1, 25), p.i("t"), p.i("r")); return o; });
          });
    R.add("filter.median", "Médiane…", "Bruit", {P::Int("r", "Rayon (px)", 1, 20, 2)},
          [](const cv::Mat& s, const Params& p) {
              return spatial(s, [&](const cv::Mat& m) { cv::Mat o; cv::medianBlur(m, o, 2 * p.i("r") + 1); return o; });
          });

    // ---------------------------------------------------------------- Netteté
    R.add("filter.sharpen", "Accentuation", "Netteté", {},
          [](const cv::Mat& s, const Params&) {
              cv::Mat k = (cv::Mat_<float>(3, 3) << 0, -1, 0, -1, 5, -1, 0, -1, 0);
              return onBGR(s, [&](const cv::Mat& m) { cv::Mat o; cv::filter2D(m, o, -1, k); return o; });
          });
    R.add("filter.unsharp", "Masque flou…", "Netteté",
          {P::Int("amount", "Gain (%)", 1, 500, 100), P::Dbl("r", "Rayon (px)", 0.1, 100, 1.5, 1), P::Int("t", "Seuil", 0, 255, 0)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat f, b, d;
                  m.convertTo(f, CV_32F);
                  cv::GaussianBlur(f, b, cv::Size(0, 0), p.d("r"));
                  d = f - b;
                  cv::Mat ad = cv::abs(d), mask;
                  cv::Mat gray; cv::cvtColor(ad, gray, cv::COLOR_BGR2GRAY);
                  cv::threshold(gray, mask, p.i("t") - 0.5, 1.0, cv::THRESH_BINARY);
                  cv::cvtColor(mask, mask, cv::COLOR_GRAY2BGR);
                  cv::Mat o = f + d.mul(mask) * (p.d("amount") / 100.0), o8;
                  o.convertTo(o8, CV_8U);
                  return o8;
              });
          });
    R.add("filter.detail", "Amélioration des détails…", "Netteté", {P::Dbl("s", "Étendue", 1, 200, 10, 0), P::Dbl("r", "Intensité", 0.01, 1, 0.15, 2)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) { cv::Mat o; cv::detailEnhance(m, o, float(p.d("s")), float(p.d("r"))); return o; });
          });

    // ---------------------------------------------------------------- Bruit
    R.add("filter.noise", "Ajout de bruit…", "Bruit",
          {P::Dbl("amount", "Quantité (%)", 0.1, 100, 10, 1), P::Choice("dist", "Distribution", {"Uniforme", "Gaussienne"}, 1), P::Bool("mono", "Monochromatique", false)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::RNG rng(4242);
                  cv::Mat n(m.size(), CV_32FC3);
                  double sigma = p.d("amount") * 2.55 / (p.i("dist") == 1 ? 1.0 : 1.7);
                  if (p.i("dist") == 1) rng.fill(n, cv::RNG::NORMAL, 0, sigma); else rng.fill(n, cv::RNG::UNIFORM, -sigma * 1.7, sigma * 1.7);
                  if (p.b("mono")) { cv::Mat g; cv::cvtColor(n, g, cv::COLOR_BGR2GRAY); cv::cvtColor(g, n, cv::COLOR_GRAY2BGR); }
                  cv::Mat f, o; m.convertTo(f, CV_32F); f += n; f.convertTo(o, CV_8U);
                  return o;
              });
          });
    R.add("filter.denoise", "Réduction du bruit…", "Bruit", {P::Dbl("h", "Intensité", 1, 30, 8, 0)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) { cv::Mat o; cv::fastNlMeansDenoisingColored(m, o, float(p.d("h")), float(p.d("h")), 7, 21); return o; });
          });
    R.add("filter.despeckle", "Antipoussière", "Bruit", {},
          [](const cv::Mat& s, const Params&) { return spatial(s, [](const cv::Mat& m) { cv::Mat o; cv::medianBlur(m, o, 3); return o; }); });

    // ---------------------------------------------------------------- Stylisation
    R.add("filter.emboss", "Relief…", "Stylisation", {P::Int("angle", "Angle (°)", -180, 180, 135), P::Dbl("h", "Hauteur", 0.5, 10, 2, 1)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  double a = p.d("angle") * CV_PI / 180, dx = std::cos(a), dy = -std::sin(a);
                  cv::Mat k(3, 3, CV_32F);
                  for (int i = -1; i <= 1; ++i) for (int j = -1; j <= 1; ++j) k.at<float>(i + 1, j + 1) = float(-(dx * j + dy * i) * p.d("h"));
                  cv::Mat g, gf, r, o;
                  cv::cvtColor(m, g, cv::COLOR_BGR2GRAY); g.convertTo(gf, CV_32F);
                  cv::filter2D(gf, r, CV_32F, k); r += 128; r.convertTo(o, CV_8U);
                  cv::cvtColor(o, o, cv::COLOR_GRAY2BGR);
                  return o;
              });
          });
    R.add("filter.edges", "Contour (Find Edges)", "Stylisation", {},
          [](const cv::Mat& s, const Params&) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat f, gx, gy, mag, o;
                  m.convertTo(f, CV_32F);
                  cv::Sobel(f, gx, CV_32F, 1, 0); cv::Sobel(f, gy, CV_32F, 0, 1);
                  cv::sqrt(gx.mul(gx) + gy.mul(gy), mag);
                  cv::Mat inv; cv::subtract(cv::Scalar::all(255), mag, inv);
                  inv.convertTo(o, CV_8U);
                  return o;
              });
          });
    R.add("filter.solarize", "Solarisation", "Stylisation", {},
          [](const cv::Mat& s, const Params&) {
              std::array<uchar, 256> l; for (int i = 0; i < 256; ++i) l[i] = uchar(i < 128 ? i : 255 - i);
              return fx::applyLut(s, l);
          });
    R.add("filter.cartoon", "Dessin animé…", "Stylisation", {P::Int("blocks", "Épaisseur des traits", 3, 25, 9)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat gray, edges, col = m.clone(), out;
                  cv::cvtColor(m, gray, cv::COLOR_BGR2GRAY);
                  cv::medianBlur(gray, gray, 7);
                  cv::adaptiveThreshold(gray, edges, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, p.i("blocks") | 1, 7);
                  for (int i = 0; i < 2; ++i) { cv::Mat t; cv::bilateralFilter(col, t, 9, 60, 7); col = t; }
                  cv::cvtColor(edges, edges, cv::COLOR_GRAY2BGR);
                  cv::bitwise_and(col, edges, out);
                  return out;
              });
          });
    R.add("filter.pencil", "Croquis au crayon…", "Stylisation", {P::Dbl("s", "Étendue", 1, 200, 60, 0), P::Dbl("shade", "Ombrage", 0.01, 0.1, 0.03, 2), P::Bool("color", "Couleur", false)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat g, c;
                  cv::pencilSketch(m, g, c, float(p.d("s")), 0.07f, float(p.d("shade")));
                  if (p.b("color")) return c;
                  cv::cvtColor(g, g, cv::COLOR_GRAY2BGR);
                  return g;
              });
          });
    R.add("filter.watercolor", "Aquarelle…", "Stylisation", {P::Dbl("s", "Étendue", 1, 200, 60, 0), P::Dbl("r", "Contraste", 0.05, 1, 0.45, 2)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) { cv::Mat o; cv::stylization(m, o, float(p.d("s")), float(p.d("r"))); return o; });
          });

    // ---------------------------------------------------------------- Pixellisation
    R.add("filter.mosaic", "Mosaïque…", "Pixellisation", {P::Int("cell", "Taille de cellule", 2, 200, 12)},
          [](const cv::Mat& s, const Params& p) {
              return spatial(s, [&](const cv::Mat& m) {
                  cv::Mat small, o;
                  int c = p.i("cell");
                  cv::resize(m, small, cv::Size(std::max(1, m.cols / c), std::max(1, m.rows / c)), 0, 0, cv::INTER_AREA);
                  cv::resize(small, o, m.size(), 0, 0, cv::INTER_NEAREST);
                  return o;
              });
          });

    // ---------------------------------------------------------------- Déformation
    R.add("filter.twirl", "Torsion…", "Déformation", {P::Int("angle", "Angle (°)", -720, 720, 120), P::Int("radius", "Rayon (% du plus petit côté)", 5, 100, 100)},
          [](const cv::Mat& s, const Params& p) {
              double cx = s.cols / 2.0, cy = s.rows / 2.0, R0 = std::min(s.cols, s.rows) / 2.0 * p.i("radius") / 100.0, ang = p.d("angle") * CV_PI / 180;
              return warp(s, [&](int x, int y, float& sx, float& sy) {
                  double dx = x - cx, dy = y - cy, r = std::sqrt(dx * dx + dy * dy);
                  if (r >= R0) return;
                  double t = ang * (1 - r / R0) * (1 - r / R0), c = std::cos(t), sn = std::sin(t);
                  sx = float(cx + dx * c - dy * sn); sy = float(cy + dx * sn + dy * c);
              });
          });
    R.add("filter.wave", "Ondulation…", "Déformation", {P::Int("amp", "Amplitude (px)", 1, 200, 10), P::Int("len", "Longueur d'onde (px)", 5, 500, 60)},
          [](const cv::Mat& s, const Params& p) {
              double A = p.d("amp"), L = p.d("len");
              return warp(s, [&](int x, int y, float& sx, float& sy) {
                  sx = float(x + A * std::sin(2 * CV_PI * y / L)); sy = float(y + A * std::sin(2 * CV_PI * x / L));
              });
          });
    R.add("filter.spherize", "Sphérisation…", "Déformation", {P::Int("amount", "Quantité (%)", -100, 100, 60)},
          [](const cv::Mat& s, const Params& p) {
              double cx = s.cols / 2.0, cy = s.rows / 2.0, R0 = std::min(s.cols, s.rows) / 2.0, a = p.d("amount") / 100.0;
              double ex = a >= 0 ? 1 + a : 1.0 / (1 - a);
              return warp(s, [&](int x, int y, float& sx, float& sy) {
                  double dx = x - cx, dy = y - cy, r = std::sqrt(dx * dx + dy * dy);
                  if (r >= R0 || r < 1e-6) return;
                  double k = R0 * std::pow(r / R0, ex) / r;
                  sx = float(cx + dx * k); sy = float(cy + dy * k);
              });
          });
    R.add("filter.vignette", "Vignettage…", "Déformation", {P::Int("amount", "Quantité", -100, 100, 50), P::Int("mid", "Point médian", 0, 99, 40)},
          [](const cv::Mat& s, const Params& p) {
              double amt = p.d("amount") / 100.0, mid = p.d("mid") / 100.0, cx = s.cols / 2.0, cy = s.rows / 2.0, md = std::sqrt(cx * cx + cy * cy);
              cv::Mat out = s.clone();
#pragma omp parallel for
              for (int y = 0; y < s.rows; ++y) {
                  cv::Vec4b* o = out.ptr<cv::Vec4b>(y);
                  for (int x = 0; x < s.cols; ++x) {
                      double d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / md;
                      double t = std::clamp((d - mid) / (1 - mid), 0.0, 1.0); t = t * t * (3 - 2 * t);
                      double f = std::abs(amt) * t;
                      for (int c = 0; c < 3; ++c) o[x][c] = fx::clampU8(amt > 0 ? o[x][c] * (1 - f) : o[x][c] + (255 - o[x][c]) * f);
                  }
              }
              return out;
          });

    // ---------------------------------------------------------------- Divers
    R.add("filter.highpass", "Passe-haut…", "Autre", {P::Dbl("r", "Rayon (px)", 0.1, 250, 5, 1)},
          [](const cv::Mat& s, const Params& p) {
              return onBGR(s, [&](const cv::Mat& m) {
                  cv::Mat f, b, o; m.convertTo(f, CV_32F);
                  cv::GaussianBlur(f, b, cv::Size(0, 0), p.d("r"));
                  cv::Mat hp; cv::subtract(f, b, hp); cv::add(hp, cv::Scalar::all(128), hp); hp.convertTo(o, CV_8U);
                  return o;
              });
          });
    R.add("filter.maximum", "Maximum (dilatation)…", "Autre", {P::Int("r", "Rayon (px)", 1, 50, 2)},
          [](const cv::Mat& s, const Params& p) { cv::Mat o; cv::dilate(s, o, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * p.i("r") + 1, 2 * p.i("r") + 1))); return o; });
    R.add("filter.minimum", "Minimum (érosion)…", "Autre", {P::Int("r", "Rayon (px)", 1, 50, 2)},
          [](const cv::Mat& s, const Params& p) { cv::Mat o; cv::erode(s, o, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2 * p.i("r") + 1, 2 * p.i("r") + 1))); return o; });

    // ---------------------------------------------------------------- Rendu
    R.add("filter.clouds", "Nuages…", "Rendu",
          {P::Color("fg", "Couleur 1", Qt::black), P::Color("bg", "Couleur 2", Qt::white), P::Int("scale", "Échelle (px)", 8, 1000, 200), P::Int("seed", "Graine", 0, 9999, 1)},
          [](const cv::Mat& s, const Params& p) {
              cv::RNG rng(uint64(p.i("seed")) + 1);
              cv::Mat acc = cv::Mat::zeros(s.size(), CV_32F);
              double w = 1, tot = 0, sc = p.d("scale");
              for (int o = 0; o < 7 && sc >= 2; ++o, sc /= 2, w *= 0.55) {
                  cv::Mat n(std::max(2, int(s.rows / sc) + 2), std::max(2, int(s.cols / sc) + 2), CV_32F), up;
                  rng.fill(n, cv::RNG::UNIFORM, 0.f, 1.f);
                  cv::resize(n, up, s.size(), 0, 0, cv::INTER_CUBIC);
                  acc += up * w; tot += w;
              }
              acc /= tot;
              cv::normalize(acc, acc, 0, 1, cv::NORM_MINMAX);
              QColor a = p.color("fg"), b = p.color("bg");
              cv::Mat out(s.size(), CV_8UC4);
#pragma omp parallel for
              for (int y = 0; y < s.rows; ++y)
                  for (int x = 0; x < s.cols; ++x) {
                      float t = acc.at<float>(y, x);
                      out.at<cv::Vec4b>(y, x) = cv::Vec4b(fx::clampU8(a.blue() + (b.blue() - a.blue()) * t), fx::clampU8(a.green() + (b.green() - a.green()) * t),
                                                          fx::clampU8(a.red() + (b.red() - a.red()) * t), 255);
                  }
              return out;
          });
}
