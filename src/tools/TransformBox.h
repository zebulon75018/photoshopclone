#pragma once
// Cadre de transformation interactif (déplacer / mettre à l'échelle par 8 poignées / pivoter), indépendant de ce qui est
// transformé (pixels d'un calque ou contour d'une sélection). Modèle : P(l) = t + R(θ)·S(sx,sy)·l, l relatif au centre d'origine.
#include <opencv2/core.hpp>
#include <QPointF>
#include <QRectF>
#include "Tool.h"

class TransformBox {
public:
    enum Handle { None, Body, Rotate, TL, T, TR, R, BR, B, BL, L };

    void reset(const QRectF& source);
    QRectF source() const { return m_r0; }

    bool press(const ToolEvent& e);          // saisit une poignée / le corps / la rotation
    bool move(const ToolEvent& e);           // vrai si les paramètres ont changé ; met à jour le curseur au survol
    void release() { m_h = None; }
    bool dragging() const { return m_h != None; }

    // Matrice affine 2x3 (CV_64F) « coordonnées pixel d'origine → nouvelles », convention d'indices de pixels (cv::warpAffine).
    cv::Mat matrix() const;
    QPointF apply(const QPointF& docPoint) const;     // même transformation en coordonnées continues (pour l'affichage)
    void paint(QPainter& p, CanvasView* v) const;

    double scaleX() const { return m_sx; }
    double scaleY() const { return m_sy; }
    double angle() const { return m_ang; }              // radians
    void setScale(double sx, double sy) { m_sx = sx; m_sy = sy; }
    void setAngle(double rad) { m_ang = rad; }

private:
    QPointF map(const QPointF& local) const;
    Handle hit(const QPointF& widgetPos, CanvasView* v) const;
    QRectF m_r0;
    QPointF m_t;                                        // position du centre transformé
    double m_sx = 1, m_sy = 1, m_ang = 0;
    Handle m_h = None;
    QPointF m_pressPos, m_t0, m_anchorLocal, m_anchorDoc;
    double m_ang0 = 0, m_sx0 = 1, m_sy0 = 1;
};
