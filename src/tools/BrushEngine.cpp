#include "BrushEngine.h"
#include "core/MatUtil.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

using cv::Vec4b;

bool BrushEngine::begin(Document* doc, const Layer::Ptr& layer, Document::Target t, const Params& p) {
    if (!doc || !layer) return false;
    m_doc = doc; m_layer = layer; m_target = t; m_p = p;
    if (t == Document::Target::Mask && (!layer->hasMask())) { m_doc = nullptr; return false; }
    if (t == Document::Target::Mask && (m_p.mode != Mode::Erase)) m_p.mode = Mode::Paint;   // outils de retouche non supportés sur masque
    m_base = doc->beginStroke(layer, t);
    m_strokeAlpha = cv::Mat::zeros(m_base.rows, m_base.cols, CV_8UC1);
    m_smudge = cv::Mat();
    m_first = true;
    m_dirty = QRect();
    m_stepLeft = std::max(1.0, m_p.size * m_p.spacing);
    return true;
}

float BrushEngine::falloff(double dist) const {
    const double r = m_p.size / 2.0;
    if (m_p.pencil) return dist <= r ? 1.f : 0.f;
    if (m_p.hardness >= 0.99) return float(std::clamp(r - dist + 0.5, 0.0, 1.0));
    double t = dist / r;
    if (t >= 1) return 0.f;
    if (t <= m_p.hardness) return 1.f;
    double u = (t - m_p.hardness) / (1 - m_p.hardness);
    return float(1 - u * u * (3 - 2 * u));
}

void BrushEngine::strokeTo(const QPointF& p) {
    if (!m_doc) return;
    if (m_first) {
        m_first = false;
        m_p.mode == Mode::Smudge ? smudgeStamp(p) : stamp(p);
        m_last = p;
        return;
    }
    const double step = std::max(1.0, m_p.size * m_p.spacing);
    double dx = p.x() - m_last.x(), dy = p.y() - m_last.y(), len = std::hypot(dx, dy);
    if (len < 1e-6) return;
    double d = m_stepLeft;
    while (d <= len) {
        QPointF q(m_last.x() + dx * d / len, m_last.y() + dy * d / len);
        m_p.mode == Mode::Smudge ? smudgeStamp(q) : stamp(q);
        d += step;
    }
    m_stepLeft = d - len;
    m_last = p;
}

static inline Vec4b overPix(const Vec4b& b, const cv::Vec3b& col, float fa) {
    float ab = b[3] / 255.f, ar = fa + ab * (1 - fa);
    if (ar <= 0) return Vec4b(0, 0, 0, 0);
    Vec4b o;
    for (int c = 0; c < 3; ++c) o[c] = uchar(std::clamp((col[c] * fa + b[c] * ab * (1 - fa)) / ar + 0.5f, 0.f, 255.f));
    o[3] = uchar(ar * 255.f + 0.5f);
    return o;
}

void BrushEngine::stamp(const QPointF& c) {
    cv::Mat& dst = targetMat();
    const double r = m_p.size / 2.0;
    QRect bb(QPoint(int(std::floor(c.x() - r)) - 1, int(std::floor(c.y() - r)) - 1), QPoint(int(std::ceil(c.x() + r)) + 1, int(std::ceil(c.y() + r)) + 1));
    bb &= QRect(0, 0, dst.cols, dst.rows);
    if (bb.isEmpty()) return;
    const cv::Rect cr = mu::toCv(bb);
    const bool isMask = m_target == Document::Target::Mask;
    const Mode mode = m_p.mode;
    const cv::Mat& sel = m_doc->selection();
    const float flow = float(m_p.flow), op = float(m_p.opacity), strength = float(m_p.strength);
    const cv::Vec3b col(uchar(m_p.color.blue()), uchar(m_p.color.green()), uchar(m_p.color.red()));
    const uchar gray = uchar(std::lround(mu::lum(float(m_p.color.red()), float(m_p.color.green()), float(m_p.color.blue()))));

    cv::Mat region;      // source par pixel pour flou / netteté
    if (mode == Mode::Blur || mode == Mode::Sharpen) {
        cv::Rect pad = (cr + cv::Point(-6, -6) + cv::Size(12, 12)) & mu::bounds(m_base);
        cv::Mat bl;
        cv::GaussianBlur(m_base(pad), bl, cv::Size(0, 0), 1.5 + strength * 2.0);
        region = bl(cv::Rect(cr.x - pad.x, cr.y - pad.y, cr.width, cr.height)).clone();
        if (mode == Mode::Sharpen) cv::addWeighted(m_base(cr), 2.0, region, -1.0, 0, region);
    }

    for (int y = cr.y; y < cr.y + cr.height; ++y) {
        uchar* sa = m_strokeAlpha.ptr<uchar>(y);
        const uchar* sl = sel.empty() ? nullptr : sel.ptr<uchar>(y);
        for (int x = cr.x; x < cr.x + cr.width; ++x) {
            double dx = x + 0.5 - c.x(), dy = y + 0.5 - c.y();
            float fall = falloff(std::sqrt(dx * dx + dy * dy));
            if (fall <= 0.f) continue;
            float d = fall * flow;
            if (sl) d *= sl[x] / 255.f;
            if (d <= 0.f) continue;
            float s = sa[x] / 255.f;
            s = s + d * (1 - s);
            sa[x] = uchar(s * 255.f + 0.5f);
            const float fa = s * op;

            if (isMask) {
                float b = m_base.at<uchar>(y, x);
                float target = mode == Mode::Erase ? 0.f : float(gray);
                dst.at<uchar>(y, x) = uchar(b + (target - b) * fa + 0.5f);
                continue;
            }
            const Vec4b b = m_base.at<Vec4b>(y, x);
            Vec4b& o = dst.at<Vec4b>(y, x);
            switch (mode) {
            case Mode::Paint: o = overPix(b, col, fa); break;
            case Mode::Erase: o = b; o[3] = uchar(b[3] * (1 - fa) + 0.5f); break;
            case Mode::Clone: {
                int sx = x + m_p.cloneOffset.x(), sy = y + m_p.cloneOffset.y();
                if (sx < 0 || sy < 0 || sx >= m_base.cols || sy >= m_base.rows) { o = b; break; }
                Vec4b sp = m_base.at<Vec4b>(sy, sx);
                o = overPix(b, cv::Vec3b(sp[0], sp[1], sp[2]), fa * sp[3] / 255.f);
                break;
            }
            case Mode::Blur: case Mode::Sharpen: {
                Vec4b sp = region.at<Vec4b>(y - cr.y, x - cr.x);
                for (int k = 0; k < 4; ++k) o[k] = uchar(std::clamp(b[k] + (sp[k] - b[k]) * fa * (mode == Mode::Blur ? 1.f : strength + 0.2f), 0.f, 255.f) + 0.5f);
                break;
            }
            case Mode::Dodge: case Mode::Burn: {
                float l = mu::lum(b[2], b[1], b[0]) / 255.f;
                float w = m_p.range == 0 ? (1 - l) * (1 - l) : m_p.range == 2 ? l * l : 1 - std::fabs(2 * l - 1) * 0.6f;
                float e = strength * w * fa;
                o = b;
                for (int k = 0; k < 3; ++k) o[k] = uchar(mode == Mode::Dodge ? std::min(255.f, b[k] + (255 - b[k]) * e) : b[k] * (1 - e) + 0.5f);
                break;
            }
            default: break;
            }
        }
    }
    m_dirty |= bb;
    m_doc->invalidate(bb);
}

// Doigt : un tampon de couleurs "porté" par le pinceau, centré sur la position courante.
void BrushEngine::smudgeStamp(const QPointF& c) {
    cv::Mat& dst = targetMat();
    const int D = int(std::ceil(m_p.size)) + 3, half = D / 2;
    const int cx = int(std::floor(c.x())), cy = int(std::floor(c.y()));
    QRect bb(cx - half, cy - half, D, D);
    QRect clipped = bb & QRect(0, 0, dst.cols, dst.rows);
    if (clipped.isEmpty() || dst.type() != CV_8UC4) return;
    if (m_smudge.empty()) {
        m_smudge = cv::Mat::zeros(D, D, CV_8UC4);
        cv::Rect cr = mu::toCv(clipped);
        dst(cr).copyTo(m_smudge(cv::Rect(cr.x - bb.x(), cr.y - bb.y(), cr.width, cr.height)));
        return;
    }
    const cv::Mat& sel = m_doc->selection();
    const float st = float(m_p.strength) * float(m_p.opacity), pick = std::max(0.05f, 1.f - float(m_p.strength));
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            double dx = x + 0.5 - c.x(), dy = y + 0.5 - c.y();
            float a = falloff(std::sqrt(dx * dx + dy * dy)) * st;
            if (!sel.empty()) a *= sel.at<uchar>(y, x) / 255.f;
            if (a <= 0.f) continue;
            Vec4b& cur = dst.at<Vec4b>(y, x);
            Vec4b& cv_ = m_smudge.at<Vec4b>(y - bb.y(), x - bb.x());
            Vec4b before = cur;
            for (int k = 0; k < 4; ++k) {
                cur[k] = uchar(before[k] + (cv_[k] - before[k]) * a + 0.5f);
                cv_[k] = uchar(cv_[k] + (before[k] - cv_[k]) * pick * std::min(1.f, a * 2) + 0.5f);
            }
        }
    m_dirty |= clipped;
    m_doc->invalidate(clipped);
}

void BrushEngine::end(const QString& name) {
    if (!m_doc) return;
    if (!m_dirty.isEmpty()) m_doc->endStroke(name, m_layer, m_target, m_base, m_dirty);
    m_doc = nullptr; m_layer.reset(); m_base = cv::Mat(); m_strokeAlpha = cv::Mat(); m_smudge = cv::Mat();
}

void BrushEngine::cancel() {
    if (!m_doc) return;
    if (!m_dirty.isEmpty()) {
        cv::Rect r = mu::toCv(m_dirty) & mu::bounds(m_base);
        m_base(r).copyTo(targetMat()(r));
        m_doc->invalidate(m_dirty);
    }
    m_doc = nullptr; m_layer.reset(); m_base = cv::Mat(); m_strokeAlpha = cv::Mat();
}
