#include "Selection.h"
#include "MatUtil.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

namespace Sel {
static const int SH = 4;   // précision sous-pixel pour le tracé anticrénelé
static inline int fx(double v) { return int(std::lround(v * (1 << SH))); }

cv::Mat fromRect(QSize size, const QRectF& r) {
    cv::Mat m = cv::Mat::zeros(size.height(), size.width(), CV_8UC1);
    QRectF n = r.normalized();
    cv::rectangle(m, cv::Point(fx(n.left()), fx(n.top())), cv::Point(fx(n.right()) - (1 << SH), fx(n.bottom()) - (1 << SH)),
                  cv::Scalar(255), cv::FILLED, cv::LINE_8, SH);
    return m;
}

cv::Mat fromEllipse(QSize size, const QRectF& r) {
    cv::Mat m = cv::Mat::zeros(size.height(), size.width(), CV_8UC1);
    QRectF n = r.normalized();
    if (n.width() < 1 || n.height() < 1) return m;
    cv::ellipse(m, cv::Point(fx(n.center().x()), fx(n.center().y())), cv::Size(fx(n.width() / 2), fx(n.height() / 2)), 0, 0, 360,
                cv::Scalar(255), cv::FILLED, cv::LINE_AA, SH);
    return m;
}

cv::Mat fromPolygon(QSize size, const QPolygonF& poly) {
    cv::Mat m = cv::Mat::zeros(size.height(), size.width(), CV_8UC1);
    if (poly.size() < 3) return m;
    std::vector<cv::Point> pts;
    for (const QPointF& p : poly) pts.emplace_back(fx(p.x()), fx(p.y()));
    cv::fillPoly(m, std::vector<std::vector<cv::Point>>{pts}, cv::Scalar(255), cv::LINE_AA, SH);
    return m;
}

cv::Mat magicWand(const cv::Mat& bgra, cv::Point seed, int tol, bool contiguous, bool aa) {
    cv::Mat bgr(bgra.size(), CV_8UC3), out;
    if (!mu::bounds(bgra).contains(seed)) return cv::Mat();
    for (int y = 0; y < bgra.rows; ++y) {   // composite sur magenta : les zones transparentes forment une "couleur" à part
        const cv::Vec4b* s = bgra.ptr<cv::Vec4b>(y);
        cv::Vec3b* d = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < bgra.cols; ++x) {
            int a = s[x][3];
            d[x] = cv::Vec3b(uchar((s[x][0] * a + 255 * (255 - a)) / 255), uchar((s[x][1] * a) / 255), uchar((s[x][2] * a + 255 * (255 - a)) / 255));
        }
    }
    if (contiguous) {
        cv::Mat mask = cv::Mat::zeros(bgr.rows + 2, bgr.cols + 2, CV_8UC1);
        cv::floodFill(bgr, mask, seed, cv::Scalar(), nullptr, cv::Scalar::all(tol), cv::Scalar::all(tol),
                      4 | cv::FLOODFILL_MASK_ONLY | cv::FLOODFILL_FIXED_RANGE | (255 << 8));
        out = mask(cv::Rect(1, 1, bgr.cols, bgr.rows)).clone();
    } else {
        cv::Vec3b c = bgr.at<cv::Vec3b>(seed);
        cv::inRange(bgr, cv::Scalar(c[0] - tol, c[1] - tol, c[2] - tol), cv::Scalar(c[0] + tol, c[1] + tol, c[2] + tol), out);
    }
    if (aa) cv::GaussianBlur(out, out, cv::Size(3, 3), 0);
    return out;
}

bool isEmpty(const cv::Mat& s) { return s.empty() || cv::countNonZero(s) == 0; }

cv::Mat combine(const cv::Mat& cur, const cv::Mat& add, Mode mode, QSize size) {
    cv::Mat base = cur.empty() ? cv::Mat::zeros(size.height(), size.width(), CV_8UC1) : cur;
    cv::Mat out;
    switch (mode) {
    case Mode::Replace: out = add; break;
    case Mode::Add: cv::max(base, add, out); break;
    case Mode::Subtract: cv::multiply(base, cv::Scalar(255) - add, out, 1.0 / 255.0); break;
    case Mode::Intersect: cv::multiply(base, add, out, 1.0 / 255.0); break;
    }
    return isEmpty(out) ? cv::Mat() : out;
}

cv::Mat invert(const cv::Mat& sel, QSize size) {
    if (sel.empty()) return cv::Mat(size.height(), size.width(), CV_8UC1, cv::Scalar(255));
    cv::Mat out;
    cv::bitwise_not(sel, out);
    return isEmpty(out) ? cv::Mat() : out;
}

cv::Mat feather(const cv::Mat& sel, double r) {
    if (sel.empty() || r <= 0) return sel;
    cv::Mat out;
    cv::GaussianBlur(sel, out, cv::Size(0, 0), std::max(0.3, r / 2.0));
    return isEmpty(out) ? cv::Mat() : out;
}

cv::Mat grow(const cv::Mat& sel, int px) {
    if (sel.empty() || px == 0) return sel;
    cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * std::abs(px) + 1, 2 * std::abs(px) + 1));
    cv::Mat out;
    if (px > 0) cv::dilate(sel, out, k); else cv::erode(sel, out, k);
    return isEmpty(out) ? cv::Mat() : out;
}

cv::Mat smooth(const cv::Mat& sel, int r) {
    if (sel.empty() || r <= 0) return sel;
    cv::Mat out;
    cv::GaussianBlur(sel, out, cv::Size(0, 0), r);
    cv::threshold(out, out, 127, 255, cv::THRESH_BINARY);
    cv::GaussianBlur(out, out, cv::Size(3, 3), 0);
    return isEmpty(out) ? cv::Mat() : out;
}

cv::Mat translated(const cv::Mat& sel, double dx, double dy) {
    if (sel.empty()) return sel;
    cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy), out;
    cv::warpAffine(sel, out, M, sel.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    return isEmpty(out) ? cv::Mat() : out;
}

cv::Rect bounds(const cv::Mat& sel) {
    if (sel.empty()) return {};
    return cv::boundingRect(sel > 0);
}

std::vector<std::vector<cv::Point>> contours(const cv::Mat& sel) {
    std::vector<std::vector<cv::Point>> c;
    if (sel.empty()) return c;
    cv::Mat bin;
    cv::threshold(sel, bin, 127, 255, cv::THRESH_BINARY);
    cv::findContours(bin, c, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    return c;
}
}
