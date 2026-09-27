#pragma once
// Moteur de tampons ("dabs") partagé par pinceau, crayon, gomme, tampon de duplication, goutte d'eau,
// netteté, doigt, densité -/+. L'opacité s'applique au trait entier (comme Photoshop) ; le flux s'accumule par tampon.
#include <opencv2/core.hpp>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include "core/Document.h"

class BrushEngine {
public:
    enum class Mode { Paint, Erase, Clone, Blur, Sharpen, Smudge, Dodge, Burn };
    struct Params {
        double size = 30, hardness = 0.8, opacity = 1, flow = 1, spacing = 0.25, strength = 0.5;
        bool pencil = false;
        Mode mode = Mode::Paint;
        QColor color = Qt::black;
        int range = 1;                 // dodge/burn : 0 ombres, 1 tons moyens, 2 hautes lumières
        QPoint cloneOffset;            // source = position + offset
    };

    bool begin(Document* doc, const Layer::Ptr& layer, Document::Target target, const Params& p);
    void strokeTo(const QPointF& p);
    bool active() const { return m_doc != nullptr; }
    void end(const QString& undoName);
    void cancel();
    void setCloneOffset(QPoint o) { m_p.cloneOffset = o; }

private:
    void stamp(const QPointF& c);
    void smudgeStamp(const QPointF& c);
    float falloff(double dist) const;
    cv::Mat& targetMat() { return m_target == Document::Target::Mask ? m_layer->mask : m_layer->image; }

    Document* m_doc = nullptr;
    Layer::Ptr m_layer;
    Document::Target m_target = Document::Target::Pixels;
    Params m_p;
    cv::Mat m_base, m_strokeAlpha, m_smudge;
    QPointF m_last;
    double m_stepLeft = 0;
    bool m_first = true;
    QRect m_dirty;
};
