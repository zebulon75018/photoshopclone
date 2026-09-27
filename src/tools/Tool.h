#pragma once
#include <QObject>
#include <QCursor>
#include <QKeyEvent>
#include <QPainter>
#include <QPointF>
#include "core/Document.h"
#include "core/Workspace.h"

class CanvasView;
class OptionsBar;

struct ToolEvent {
    QPointF pos;                 // coordonnées image (flottantes)
    QPointF widgetPos;           // coordonnées du viewport
    Qt::KeyboardModifiers mods;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    CanvasView* view = nullptr;
    Document* doc = nullptr;
    bool shift() const { return mods & Qt::ShiftModifier; }
    bool alt() const { return mods & Qt::AltModifier; }
    bool ctrl() const { return mods & Qt::ControlModifier; }
};

// Classe de base de tous les outils (patron Stratégie). Les outils reçoivent les événements de la vue
// convertis en coordonnées image et dessinent leur aperçu dans paintOverlay().
class Tool : public QObject {
    Q_OBJECT
public:
    explicit Tool(QObject* parent = nullptr) : QObject(parent) {}
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QCursor cursor() const { return Qt::CrossCursor; }

    virtual void press(const ToolEvent&) {}
    virtual void move(const ToolEvent&) {}
    virtual void release(const ToolEvent&) {}
    virtual void doubleClick(const ToolEvent&) {}
    virtual bool keyPress(QKeyEvent*, CanvasView*) { return false; }
    virtual void paintOverlay(QPainter&, CanvasView*) {}
    virtual void activate() {}
    virtual void deactivate() {}
    virtual void cancel() {}                 // abandonne l'opération en cours
    virtual void flush() {}                  // valide l'opération en cours (appelé avant une action de menu)
    virtual void buildOptions(OptionsBar&) {}

protected:
    Workspace& ws() const { return Workspace::instance(); }
    ToolSettings& st() const { return Workspace::instance().settings; }
};
