#pragma once
// Commandes d'historique (QUndoCommand).
//
// INVARIANT MÉMOIRE : les commandes conservent des *en-têtes* cv::Mat partagés (aucune copie de pixels) sauf
// PixelCommand qui stocke un différentiel rectangulaire. Comme la pile d'annulation est linéaire, un tampon
// n'est modifié sur place que lorsqu'il est l'état courant : toutes les commandes postérieures sont alors annulées.
#include <QUndoCommand>
#include "Layer.h"
#include "Document.h"
#include "MatUtil.h"

class PixelCommand : public QUndoCommand {
public:
    PixelCommand(Document* d, Layer::Ptr l, Document::Target t, cv::Rect r, cv::Mat before, cv::Mat after, const QString& name)
        : QUndoCommand(name), m_doc(d), m_layer(std::move(l)), m_target(t), m_rect(r), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { apply(m_before); }
    void redo() override { apply(m_after); }
private:
    void apply(const cv::Mat& src) {
        cv::Mat& dst = m_target == Document::Target::Mask ? m_layer->mask : m_layer->image;
        if (dst.empty() || src.empty()) return;
        src.copyTo(dst(m_rect));
        m_doc->invalidate(mu::toQt(m_rect));
        m_doc->notifyLayerPixels(m_layer.get());
    }
    Document* m_doc; Layer::Ptr m_layer; Document::Target m_target; cv::Rect m_rect; cv::Mat m_before, m_after;
};

class LayerStateCommand : public QUndoCommand {
public:
    LayerStateCommand(Document* d, Layer::Ptr l, LayerState before, LayerState after, const QString& name)
        : QUndoCommand(name), m_doc(d), m_layer(std::move(l)), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { m_doc->applyLayerState(m_layer, m_before); }
    void redo() override { m_doc->applyLayerState(m_layer, m_after); }
private:
    Document* m_doc; Layer::Ptr m_layer; LayerState m_before, m_after;
};

class StructureCommand : public QUndoCommand {
public:
    StructureCommand(Document* d, Document::Structure before, Document::Structure after, const QString& name)
        : QUndoCommand(name), m_doc(d), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { m_doc->applyStructure(m_before); }
    void redo() override { m_doc->applyStructure(m_after); }
private:
    Document* m_doc; Document::Structure m_before, m_after;
};

class SelectionCommand : public QUndoCommand {
public:
    SelectionCommand(Document* d, cv::Mat before, cv::Mat after, const QString& name)
        : QUndoCommand(name), m_doc(d), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { m_doc->applySelection(m_before); }
    void redo() override { m_doc->applySelection(m_after); }
private:
    Document* m_doc; cv::Mat m_before, m_after;
};
