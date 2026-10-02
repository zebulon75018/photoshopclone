#include "FloatingContent.h"
#include "core/MatUtil.h"
#include "core/Selection.h"
#include "core/Workspace.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(FloatingContent) }; }   // traductions hors classes QObject (voir translations/)

bool FloatingContent::begin(Document* doc) {
    QString why;
    auto l = doc->editableLayer(&why);
    if (!l) { Workspace::instance().message(why); return false; }
    m_doc = doc; m_layer = l; m_selBefore = doc->selection();
    if (doc->hasSelection()) {
        m_bb = Sel::bounds(doc->selection()) & mu::bounds(l->image);
        if (m_bb.empty()) { m_doc = nullptr; return false; }
        m_patch = l->image(m_bb).clone();
        // Part soulevée = min(alpha, sélection) ; part laissée en place = alpha − part soulevée.
        // (identique à alpha·sél pour une sélection nette ou un calque opaque, mais sans liseré fantôme quand le contenu
        //  et la sélection ont les mêmes bords doux, par exemple après une première transformation)
        cv::Mat a, lifted, rest;
        cv::Mat selRoi = doc->selection()(m_bb);
        cv::extractChannel(m_patch, a, 3);
        cv::min(a, selRoi, lifted);
        cv::subtract(a, lifted, rest);
        cv::insertChannel(lifted, m_patch, 3);
        m_hole = l->image.clone();
        cv::Mat holeRoi = m_hole(m_bb);
        cv::insertChannel(rest, holeRoi, 3);
        m_hasHole = true;
    } else {
        cv::Mat a; cv::extractChannel(l->image, a, 3);
        m_bb = cv::boundingRect(a > 0);
        if (m_bb.empty()) { Workspace::instance().message(Tr::tr("Le calque est vide.")); m_doc = nullptr; return false; }
        m_patch = l->image(m_bb).clone();
        m_hasHole = false;
    }
    m_before = l->state();
    m_before.image = l->image.clone();      // l->image sera modifiée sur place pendant l'aperçu
    m_lastRect = m_bb;
    m_M = (cv::Mat_<double>(2, 3) << 1, 0, 0, 0, 1, 0);
    return true;
}

cv::Rect FloatingContent::place(const cv::Mat& M, cv::Mat& A) const {
    // A = M * T(bb.tl) : pixel du patch -> document
    cv::Mat T = (cv::Mat_<double>(3, 3) << 1, 0, m_bb.x, 0, 1, m_bb.y, 0, 0, 1), M3 = cv::Mat::eye(3, 3, CV_64F);
    M.copyTo(M3(cv::Rect(0, 0, 3, 2)));
    cv::Mat P = M3 * T;
    std::vector<cv::Point2d> c = {{0, 0}, {double(m_bb.width), 0}, {double(m_bb.width), double(m_bb.height)}, {0, double(m_bb.height)}}, o;
    cv::transform(c, o, P(cv::Rect(0, 0, 3, 2)));
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (auto& p : o) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    cv::Rect B(int(std::floor(x0)) - 1, int(std::floor(y0)) - 1, int(std::ceil(x1 - x0)) + 3, int(std::ceil(y1 - y0)) + 3);
    B &= mu::bounds(m_layer->image);
    cv::Mat Tb = (cv::Mat_<double>(3, 3) << 1, 0, -B.x, 0, 1, -B.y, 0, 0, 1);
    cv::Mat TP = Tb * P;
    A = TP(cv::Rect(0, 0, 3, 2)).clone();
    return B;
}

void FloatingContent::update(const cv::Mat& M) {
    if (!m_doc) return;
    m_M = M.clone();
    cv::Mat A;
    cv::Rect B = place(M, A);
    cv::Rect region = m_lastRect | B;
    if (m_hasHole) region |= m_bb;
    region &= mu::bounds(m_layer->image);
    if (m_hasHole) m_hole(region).copyTo(m_layer->image(region)); else m_layer->image(region).setTo(cv::Scalar(0, 0, 0, 0));
    if (!B.empty()) {
        cv::Mat pm = m_patch.clone(), warped;
        mu::premultiply(pm);
        bool pureShift = std::fabs(M.at<double>(0, 0) - 1) < 1e-9 && std::fabs(M.at<double>(1, 1) - 1) < 1e-9 && std::fabs(M.at<double>(0, 1)) < 1e-9 && std::fabs(M.at<double>(1, 0)) < 1e-9;
        cv::warpAffine(pm, warped, A, B.size(), pureShift ? cv::INTER_NEAREST : cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
        mu::unpremultiply(warped);
        Blend::over(m_layer->image, warped, B.tl(), B);
    }
    m_lastRect = B;
    m_doc->invalidate(mu::toQt(region));
}

void FloatingContent::commit(const QString& name) {
    if (!m_doc) return;
    Document* d = m_doc;
    bool shift = std::fabs(m_M.at<double>(0, 0) - 1) < 1e-9 && std::fabs(m_M.at<double>(1, 1) - 1) < 1e-9 && std::fabs(m_M.at<double>(0, 1)) < 1e-9 && std::fabs(m_M.at<double>(1, 0)) < 1e-9;
    if (m_layer->isText()) {
        if (shift && m_selBefore.empty()) m_layer->text->pos += QPointF(m_M.at<double>(0, 2), m_M.at<double>(1, 2));
        else m_layer->text.reset();
    }
    d->undoStack()->beginMacro(name);
    d->pushLayerChange(name, m_layer, m_before);
    if (!m_selBefore.empty()) {
        cv::Mat ns;
        cv::warpAffine(m_selBefore, ns, m_M, m_selBefore.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
        d->setSelection(ns, name);
    }
    d->undoStack()->endMacro();
    m_doc = nullptr; m_layer.reset(); m_patch = m_hole = cv::Mat();
}

void FloatingContent::cancel() {
    if (!m_doc) return;
    m_layer->image = m_before.image;
    m_doc->invalidateAll();
    m_doc = nullptr; m_layer.reset(); m_patch = m_hole = cv::Mat();
}
