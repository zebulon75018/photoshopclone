#pragma once
#include <QDoubleSpinBox>
#include <QPolygonF>
#include <QSlider>
#include <QWidget>

// Curseur + champ numérique synchronisés.
class SliderSpin : public QWidget {
    Q_OBJECT
public:
    SliderSpin(double min, double max, double value, int decimals = 0, QWidget* parent = nullptr);
    double value() const { return m_spin->value(); }
    void setValue(double v);
signals:
    void valueChanged(double);
private:
    QSlider* m_slider; QDoubleSpinBox* m_spin; double m_scale; bool m_busy = false;
};

// Éditeur de courbe (points de contrôle dans [0,1]²) : clic = ajouter, glisser = déplacer, double-clic / clic droit = supprimer.
class CurveWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurveWidget(QWidget* parent = nullptr);
    QPolygonF points() const { return m_pts; }
    void setPoints(const QPolygonF& p) { m_pts = p; update(); }
    QSize sizeHint() const override { return QSize(260, 260); }
signals:
    void changed();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override { m_drag = -1; }
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    QPointF toPt(const QPointF& w) const;
    QPointF toW(const QPointF& p) const;
    int hit(const QPointF& w) const;
    QPolygonF m_pts; int m_drag = -1;
};
