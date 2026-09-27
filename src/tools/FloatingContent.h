#pragma once
// Contenu "soulevé" d'un calque (pixels sélectionnés, ou tout le contenu du calque) que l'on peut déplacer/transformer
// de façon interactive avant validation. Utilisé par l'outil Déplacement et la Transformation manuelle.
#include <opencv2/core.hpp>
#include <QRectF>
#include "core/Document.h"

class FloatingContent {
public:
    bool begin(Document* doc);
    bool active() const { return m_doc != nullptr; }
    QRectF sourceRect() const { return QRectF(m_bb.x, m_bb.y, m_bb.width, m_bb.height); }
    // M : matrice affine 2x3 (CV_64F) qui envoie les coordonnées document d'origine vers les nouvelles.
    void update(const cv::Mat& M);
    void commit(const QString& name);
    void cancel();
    bool hadSelection() const { return !m_selBefore.empty(); }

private:
    cv::Rect place(const cv::Mat& M, cv::Mat& A) const;
    Document* m_doc = nullptr;
    Layer::Ptr m_layer;
    LayerState m_before;
    cv::Mat m_patch, m_hole, m_selBefore, m_M;
    cv::Rect m_bb, m_lastRect;
    bool m_hasHole = false;
};
