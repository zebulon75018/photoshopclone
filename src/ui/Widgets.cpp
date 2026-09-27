#include "Widgets.h"
#include "effects/Effect.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

SliderSpin::SliderSpin(double mn, double mx, double v, int dec, QWidget* parent) : QWidget(parent), m_scale(std::pow(10.0, dec)) {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(int(std::lround(mn * m_scale)), int(std::lround(mx * m_scale)));
    m_spin = new QDoubleSpinBox;
    m_spin->setRange(mn, mx); m_spin->setDecimals(dec); m_spin->setKeyboardTracking(false);
    m_spin->setFixedWidth(dec ? 78 : 64);
    l->addWidget(m_slider, 1); l->addWidget(m_spin);
    setValue(v);
    connect(m_slider, &QSlider::valueChanged, this, [this](int iv) {
        if (m_busy) return;
        m_busy = true; m_spin->setValue(iv / m_scale); m_busy = false;
        emit valueChanged(m_spin->value());
    });
    connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double dv) {
        if (m_busy) return;
        m_busy = true; m_slider->setValue(int(std::lround(dv * m_scale))); m_busy = false;
        emit valueChanged(dv);
    });
}

void SliderSpin::setValue(double v) {
    m_busy = true;
    m_spin->setValue(v); m_slider->setValue(int(std::lround(v * m_scale)));
    m_busy = false;
}

// ---------------------------------------------------------------------------------------------- CurveWidget
CurveWidget::CurveWidget(QWidget* parent) : QWidget(parent) {
    m_pts << QPointF(0, 0) << QPointF(1, 1);
    setMinimumSize(200, 200);
    setCursor(Qt::CrossCursor);
}

QPointF CurveWidget::toW(const QPointF& p) const { return QPointF(8 + p.x() * (width() - 16), height() - 8 - p.y() * (height() - 16)); }
QPointF CurveWidget::toPt(const QPointF& w) const {
    return QPointF(std::clamp((w.x() - 8) / (width() - 16), 0.0, 1.0), std::clamp((height() - 8 - w.y()) / (height() - 16), 0.0, 1.0));
}
int CurveWidget::hit(const QPointF& w) const {
    for (int i = 0; i < m_pts.size(); ++i) if (QLineF(w, toW(m_pts[i])).length() <= 9) return i;
    return -1;
}

void CurveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(30, 30, 30));
    p.setPen(QPen(QColor(70, 70, 70), 1));
    for (int i = 0; i <= 4; ++i) {
        p.drawLine(toW(QPointF(i / 4.0, 0)), toW(QPointF(i / 4.0, 1)));
        p.drawLine(toW(QPointF(0, i / 4.0)), toW(QPointF(1, i / 4.0)));
    }
    p.setPen(QPen(QColor(100, 100, 100), 1, Qt::DashLine)); p.drawLine(toW({0, 0}), toW({1, 1}));
    auto lut = curveLut(m_pts);
    QPolygonF poly;
    for (int i = 0; i < 256; ++i) poly << toW(QPointF(i / 255.0, lut[i] / 255.0));
    p.setPen(QPen(QColor(235, 235, 235), 1.6)); p.drawPolyline(poly);
    p.setBrush(QColor(20, 20, 20)); p.setPen(QPen(Qt::white, 1.4));
    for (const QPointF& pt : m_pts) p.drawEllipse(toW(pt), 4.5, 4.5);
}

void CurveWidget::mousePressEvent(QMouseEvent* e) {
    int h = hit(e->localPos());
    if (e->button() == Qt::RightButton) {
        if (h > 0 && h < m_pts.size() - 1) { m_pts.remove(h); update(); emit changed(); }
        return;
    }
    if (h < 0) {   // ajout d'un point, en conservant l'ordre en x
        QPointF np = toPt(e->localPos());
        int at = 0; while (at < m_pts.size() && m_pts[at].x() < np.x()) ++at;
        m_pts.insert(at, np); h = at;
        emit changed();
    }
    m_drag = h; update();
}

void CurveWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag < 0) return;
    QPointF np = toPt(e->localPos());
    double lo = m_drag > 0 ? m_pts[m_drag - 1].x() + 0.004 : 0.0, hi = m_drag < m_pts.size() - 1 ? m_pts[m_drag + 1].x() - 0.004 : 1.0;
    if (m_drag == 0) hi = std::min(hi, 0.0 + 0.996);
    np.setX(std::clamp(np.x(), lo, hi));
    if (m_drag == 0 || m_drag == m_pts.size() - 1) np.setX(m_pts[m_drag].x());   // extrémités : mouvement vertical uniquement
    m_pts[m_drag] = np;
    update(); emit changed();
}

void CurveWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    int h = hit(e->localPos());
    if (h > 0 && h < m_pts.size() - 1) { m_pts.remove(h); update(); emit changed(); }
}
