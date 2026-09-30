#pragma once
// État global partagé de l'application : couleurs premier plan / arrière-plan, réglages des outils, vue courante.
#include <QColor>
#include <QObject>
#include <QString>
#include "core/Selection.h"

class CanvasView;
class ToolManager;
class Document;
class QWidget;

struct ToolSettings {
    // pinceau & assimilés
    int size = 30, hardness = 80, opacity = 100, flow = 100, spacing = 25;
    // sélection
    Sel::Mode selMode = Sel::Mode::Replace;
    int feather = 0;
    bool antiAlias = true;
    int tolerance = 32;
    bool contiguous = true;
    bool sampleAll = false;
    bool samSampleAll = true;      // sélection par IA : analyser l'image fusionnée (sinon le calque actif)
    // transformation de la sélection : transformer aussi les pixels du calque actif contenus dans la sélection
    bool selTransformContent = true;
    // dégradé
    int gradientType = 0;          // 0 linéaire 1 radial 2 angulaire 3 réfléchi 4 losange
    int gradientColors = 0;        // 0 PP→AP, 1 PP→transparent
    bool gradientReverse = false;
    // formes
    int shapeKind = 0;             // 0 rect 1 rect arrondi 2 ellipse 3 ligne
    bool shapeFill = true, shapeStroke = false;
    int strokeWidth = 4, cornerRadius = 20;
    // texte
    QString fontFamily = "Sans Serif";
    int fontSize = 48;
    bool bold = false, italic = false;
    int textAlign = 0;
    // retouche
    int strength = 50;             // 1..100 (exposition / force)
    int range = 1;                 // 0 ombres 1 tons moyens 2 hautes lumières
    bool aligned = true;           // tampon
    // pipette
    int sampleSize = 0;            // index : 0 point, 1 = 3x3, 2 = 5x5, 3 = 11x11
};

class Workspace : public QObject {
    Q_OBJECT
public:
    static Workspace& instance();

    QColor fg{Qt::black}, bg{Qt::white};
    ToolSettings settings;

    void setFg(const QColor& c) { fg = c; emit colorsChanged(); }
    void setBg(const QColor& c) { bg = c; emit colorsChanged(); }
    void swapColors() { std::swap(fg, bg); emit colorsChanged(); }
    void resetColors() { fg = Qt::black; bg = Qt::white; emit colorsChanged(); }

    ToolManager* tools();
    CanvasView* view() const { return m_view; }
    void setView(CanvasView* v);
    Document* doc() const;

    // Fenêtre principale (hôte des dialogues déplaçables) et verrou "dialogue ouvert" (les outils sont inhibés, pan/zoom restent possibles)
    QWidget* mainWindow() const { return m_mainWindow; }
    void setMainWindow(QWidget* w) { m_mainWindow = w; }
    bool modalLocked() const { return m_modal > 0; }
    void pushModal() { if (m_modal++ == 0) emit modalChanged(true); }
    void popModal() { if (--m_modal == 0) emit modalChanged(false); }

    void message(const QString& m) { emit messageRequested(m); }
    void settingsModified() { emit settingsChanged(); }

signals:
    void colorsChanged();
    void settingsChanged();
    void viewChanged();
    void messageRequested(const QString&);
    void modalChanged(bool);

private:
    Workspace() = default;
    CanvasView* m_view = nullptr;
    ToolManager* m_tools = nullptr;
    QWidget* m_mainWindow = nullptr;
    int m_modal = 0;
};
