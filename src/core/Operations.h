#pragma once
// Opérations de haut niveau sur un Document (chaque appel = une entrée d'historique).
// Aucune dépendance à l'interface graphique : testable et réutilisable (scripts, macros...).
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include "Document.h"
#include "effects/Effect.h"

namespace Ops {
Document* makeDocument(QSize size, int background /*0 blanc, 1 couleur, 2 transparent*/, const QColor& bg);

// --- Calques
Layer::Ptr addLayer(Document*, const QString& name = QString());
void addLayerWithImage(Document*, const cv::Mat& bgra, const QString& name);
void duplicateLayer(Document*);
void layerViaCopy(Document*, bool cut);      // Ctrl+J / Ctrl+Maj+J
void deleteLayer(Document*);
void mergeDown(Document*);
void mergeVisible(Document*);
void flatten(Document*);
void reorderByIds(Document*, const std::vector<int>& idsBottomToTop);
void moveLayer(Document*, int delta);            // +1 vers le haut, -1 vers le bas
void moveLayerToEnd(Document*, bool top);
void setProps(Document*, const Layer::Ptr&, const LayerProps&, const QString& undoName);
void addMask(Document*, bool hideAll);
void deleteMask(Document*, bool apply);
void flipLayer(Document*, bool horizontal);
Layer::Ptr commitText(Document*, const Layer::Ptr& existing, const TextData& td);
void rasterizeText(Document*);

// --- Image
void resizeImage(Document*, QSize, int cvInterpolation);
void resizeCanvas(Document*, QSize, int anchor /*0..8, ligne par ligne*/);
void cropTo(Document*, const QRect&);
void rotateImage(Document*, int degreesCW);     // 90, 180, 270
void flipImage(Document*, bool horizontal);

// --- Sélection
void selectAll(Document*);
void deselect(Document*);
void invertSelection(Document*);
void modifySelection(Document*, int kind /*0 adoucir(feather) 1 étendre 2 contracter 3 lisser*/, double amount);

// --- Édition
void fillSelection(Document*, const QColor&, const QString& undoName);
void clearSelection(Document*);
void copy(Document*, bool merged);
void cut(Document*);
Layer::Ptr paste(Document*, bool inPlace, QPoint center);
void applyEffect(Document*, const Effect&, const Params&);
void fillMask(Document*, const cv::Mat& mask, const QColor&, double opacity, const QString& undoName);

QString uniqueLayerName(const Document*, const QString& base);
}
