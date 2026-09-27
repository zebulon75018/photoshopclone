#pragma once
#include <functional>
#include <vector>
#include <QObject>
#include <QImage>
#include <QRect>
#include <QUndoStack>
#include "Layer.h"

// Modèle d'un document : pile de calques, sélection, image composite (cache), historique.
class Document : public QObject {
    Q_OBJECT
public:
    enum class Target { Pixels, Mask };
    using LayerList = std::vector<Layer::Ptr>;
    struct Structure { QSize size; LayerList layers; int active = 0; };

    explicit Document(QSize size, QObject* parent = nullptr);

    QSize size() const { return m_size; }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }
    QString path;                                  // fichier d'origine (vide = jamais enregistré)
    QString title() const;
    bool isModified() const { return !m_undo.isClean(); }
    QUndoStack* undoStack() { return &m_undo; }

    // --- Calques (du bas vers le haut)
    const LayerList& layers() const { return m_layers; }
    int activeIndex() const { return m_active; }
    void setActiveIndex(int i);
    Layer::Ptr activeLayer() const;
    Layer::Ptr layerById(int id) const;
    int indexOf(const Layer* l) const;
    Layer::Ptr editableLayer(QString* whyNot = nullptr) const;   // calque actif s'il est visible et non verrouillé

    // --- Composite
    const QImage& display() const { return m_display; }   // ARGB32 prémultiplié, taille du document
    cv::Mat compositeCopy() const { return m_comp.clone(); }
    const cv::Mat& composite() const { return m_comp; }
    void invalidate(const QRect& r);
    void invalidateAll() { invalidate(rect()); }

    // --- Sélection (masque 8 bits ou vide)
    const cv::Mat& selection() const { return m_sel; }
    bool hasSelection() const { return !m_sel.empty(); }
    void setSelection(const cv::Mat& s, const QString& undoName);
    void reselect();
    void applySelection(const cv::Mat& s);             // sans historique (utilisé par les commandes)

    // --- Historique / mutations
    Structure structure() const { return {m_size, m_layers, m_active}; }
    void applyStructure(const Structure& s);
    void doStructural(const QString& name, const std::function<void()>& change);   // change() modifie m_layers/active via les accesseurs ci-dessous
    LayerList& mutableLayers() { return m_layers; }
    void setSizeInternal(QSize s) { m_size = s; }
    void setActiveInternal(int i) { m_active = i; }

    void applyLayerState(const Layer::Ptr& l, const LayerState& s);
    void pushLayerChange(const QString& name, const Layer::Ptr& l, const LayerState& before);   // "after" = état courant
    void doLayerChange(const QString& name, const Layer::Ptr& l, const std::function<void(Layer&)>& change);

    // Traits de pinceau : snapshot complet avant, puis commit d'un différentiel.
    cv::Mat beginStroke(const Layer::Ptr& l, Target t) const;
    void endStroke(const QString& name, const Layer::Ptr& l, Target t, const cv::Mat& snapshot, const QRect& dirty);
    void notifyLayerPixels(Layer*) { emit layersChanged(); }

signals:
    void changed(const QRect& r);            // pixels du composite modifiés
    void layersChanged();                    // liste, propriétés ou vignettes
    void activeLayerChanged();
    void selectionChanged();
    void sizeChanged();

private:
    void rebuildComposite();
    void recomposite(const QRect& r);

    QSize m_size;
    LayerList m_layers;
    int m_active = 0;
    cv::Mat m_comp, m_sel, m_lastSel;
    QImage m_display;
    QUndoStack m_undo;
};
