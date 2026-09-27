#include "TransformBox.h"
#include "ui/CanvasView.h"
#include <cmath>

static QPointF rot(const QPointF& v, double a) { double c = std::cos(a), s = std::sin(a); return {v.x() * c - v.y() * s, v.x() * s + v.y() * c}; }

static QPointF handleLocal(int h, double w, double hh) {
    switch (h) {
    case TransformBox::TL: return {-w / 2, -hh / 2}; case TransformBox::T: return {0, -hh / 2};
    case TransformBox::TR: return {w / 2, -hh / 2};  case TransformBox::R: return {w / 2, 0};
    case TransformBox::BR: return {w / 2, hh / 2};   case TransformBox::B: return {0, hh / 2};
    case TransformBox::BL: return {-w / 2, hh / 2};  case TransformBox::L: return {-w / 2, 0};
    default: return {0, 0};
    }
}

void TransformBox::reset(const QRectF& r) { m_r0 = r; m_t = r.center(); m_sx = m_sy = 1; m_ang = 0; m_h = None; }

QPointF TransformBox::map(const QPointF& l) const { return m_t + rot(QPointF(l.x() * m_sx, l.y() * m_sy), m_ang); }
QPointF TransformBox::apply(const QPointF& p) const { return map(p - m_r0.center()); }

TransformBox::Handle TransformBox::hit(const QPointF& wp, CanvasView* v) const {
    for (int h = TL; h <= L; ++h)
        if (QLineF(wp, v->toWidget(map(handleLocal(h, m_r0.width(), m_r0.height())))).length() <= 8) return Handle(h);
    QPolygonF poly;
    for (int h : {TL, TR, BR, BL}) poly << v->toWidget(map(handleLocal(h, m_r0.width(), m_r0.height())));
    return poly.containsPoint(wp, Qt::OddEvenFill) ? Body : Rotate;
}

bool TransformBox::press(const ToolEvent& e) {
    m_h = hit(e.widgetPos, e.view);
    m_pressPos = e.pos; m_t0 = m_t; m_ang0 = m_ang; m_sx0 = m_sx; m_sy0 = m_sy;
    if (m_h >= TL) {
        QPointF lh = handleLocal(m_h, m_r0.width(), m_r0.height());
        m_anchorLocal = e.alt() ? QPointF(0, 0) : -lh;    // Alt : transformation depuis le centre
        m_anchorDoc = map(m_anchorLocal);
    }
    return m_h != None;
}

bool TransformBox::move(const ToolEvent& e) {
    if (m_h == None || !(e.buttons & Qt::LeftButton)) {   // survol : curseur adapté
        if (e.view && !(e.buttons & Qt::LeftButton)) {
            Handle h = hit(e.widgetPos, e.view);
            Qt::CursorShape c = Qt::ArrowCursor;
            switch (h) {
            case Body: c = Qt::SizeAllCursor; break;
            case TL: case BR: c = Qt::SizeFDiagCursor; break;
            case TR: case BL: c = Qt::SizeBDiagCursor; break;
            case T: case B: c = Qt::SizeVerCursor; break;
            case L: case R: c = Qt::SizeHorCursor; break;
            case Rotate: c = Qt::PointingHandCursor; break;
            default: break;
            }
            e.view->viewport()->setCursor(c);
        }
        return false;
    }
    if (m_h == Body) {
        m_t = m_t0 + (e.pos - m_pressPos);
    } else if (m_h == Rotate) {
        QPointF a = m_pressPos - m_t0, b = e.pos - m_t0;
        m_ang = m_ang0 + std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x());
        if (e.shift()) m_ang = std::round(m_ang / (CV_PI / 12)) * (CV_PI / 12);   // pas de 15°
    } else {
        QPointF lh = handleLocal(m_h, m_r0.width(), m_r0.height());
        QPointF q = rot(e.pos - m_anchorDoc, -m_ang0);
        double nsx = m_sx0, nsy = m_sy0;
        double dx = lh.x() - m_anchorLocal.x(), dy = lh.y() - m_anchorLocal.y();
        if (lh.x() != 0 && std::fabs(dx) > 1e-9) nsx = q.x() / dx;
        if (lh.y() != 0 && std::fabs(dy) > 1e-9) nsy = q.y() / dy;
        if (e.shift() && lh.x() != 0 && lh.y() != 0) {     // proportionnel
            double k = std::max(std::fabs(nsx / m_sx0), std::fabs(nsy / m_sy0));
            nsx = std::copysign(m_sx0 * k, nsx); nsy = std::copysign(m_sy0 * k, nsy);
        }
        if (std::fabs(nsx) < 0.01) nsx = 0.01;
        if (std::fabs(nsy) < 0.01) nsy = 0.01;
        m_sx = nsx; m_sy = nsy;
        m_t = m_anchorDoc - rot(QPointF(m_anchorLocal.x() * m_sx, m_anchorLocal.y() * m_sy), m_ang0);
    }
    return true;
}

cv::Mat TransformBox::matrix() const {
    double c = std::cos(m_ang), s = std::sin(m_ang);
    double a = c * m_sx, b = -s * m_sy, d = s * m_sx, e = c * m_sy;          // L = R·S
    QPointF c0 = m_r0.center();
    double tx = m_t.x() - (a * c0.x() + b * c0.y()), ty = m_t.y() - (d * c0.x() + e * c0.y());
    // conversion en indices de pixels : i' = L·(i + ½) + t − ½  (centres de pixels à +½)
    tx += 0.5 * (a + b) - 0.5; ty += 0.5 * (d + e) - 0.5;
    return (cv::Mat_<double>(2, 3) << a, b, tx, d, e, ty);
}

void TransformBox::paint(QPainter& p, CanvasView* v) const {
    QPolygonF poly;
    for (int h : {TL, TR, BR, BL}) poly << v->toWidget(map(handleLocal(h, m_r0.width(), m_r0.height())));
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 3)); p.drawPolygon(poly);
    p.setPen(QPen(Qt::white, 1)); p.drawPolygon(poly);
    p.setPen(QPen(Qt::black, 1)); p.setBrush(Qt::white);
    for (int h = TL; h <= L; ++h) {
        QPointF c = v->toWidget(map(handleLocal(h, m_r0.width(), m_r0.height())));
        p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
    }
    QPointF ctr = v->toWidget(m_t);
    p.setPen(Qt::white); p.drawLine(ctr + QPointF(-6, 0), ctr + QPointF(6, 0)); p.drawLine(ctr + QPointF(0, -6), ctr + QPointF(0, 6));
}
