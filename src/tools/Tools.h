#pragma once
// Déclarations de tous les outils concrets.
#include <QPolygonF>
#include <QRectF>
#include "Tool.h"
#include "BrushEngine.h"
#include "FloatingContent.h"
#include "TransformBox.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include "ui/CanvasView.h"

// ----------------------------------------------------------------------------- déplacement / transformation
class MoveTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "move"; }
    QString name() const override { return "Déplacement (V)"; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    bool keyPress(QKeyEvent*, CanvasView*) override;
    void cancel() override { m_fc.cancel(); m_drag = false; }
    void buildOptions(OptionsBar&) override;
private:
    FloatingContent m_fc; QPointF m_start; bool m_drag = false;
};

class TransformTool : public Tool {   // Édition > Transformation manuelle (Ctrl+T) : transforme les PIXELS
    Q_OBJECT
public:
    QString id() const override { return "transform"; }
    QString name() const override { return "Transformation manuelle"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override { m_box.release(); }
    void doubleClick(const ToolEvent&) override { commit(); }
    bool keyPress(QKeyEvent*, CanvasView*) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void activate() override;
    void deactivate() override;
    void cancel() override;
    void flush() override { if (m_fc.active()) commit(); }
    void commit();
    bool begin(Document*);
    bool active() const { return m_fc.active(); }
signals:
    void finished();
private:
    void apply();
    FloatingContent m_fc; TransformBox m_box; Document* m_doc = nullptr; bool m_dirty = false;
};

// Transformation de la SÉLECTION : agrandir / réduire / pivoter / déplacer le contour ET (option, activée par défaut)
// les pixels du calque actif contenus dans la sélection. Contour et pixels sont validés en une seule étape d'historique.
class SelectionTransformTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "sel_transform"; }
    QString name() const override { return "Transformation de la sélection (M)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override { m_box.release(); }
    void doubleClick(const ToolEvent&) override { commit(true); }
    bool keyPress(QKeyEvent*, CanvasView*) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void activate() override { begin(); }
    void deactivate() override { commit(false); }       // changer d'outil valide la transformation en cours
    void cancel() override { endSession(); }
    void flush() override { if (m_active && m_dirty) commit(true); }
    void buildOptions(OptionsBar&) override;
private slots:
    void onSelectionChanged();
private:
    bool begin();
    void commit(bool restart);
    void endSession();
    void syncSpins();
    void applyNumeric(bool fromX);
    void boxChanged();
    bool ensureContent();
    FloatingContent m_fc;                 // pixels du calque actif contenus dans la sélection (soulevés au premier changement)
    bool m_contentFailed = false;
    TransformBox m_box;
    QPointer<Document> m_doc; QPointer<CanvasView> m_view;
    bool m_active = false, m_dirty = false, m_committing = false, m_syncing = false;
    std::vector<std::vector<cv::Point>> m_contours;
    QPointer<QDoubleSpinBox> m_spX, m_spY, m_spA; QPointer<QCheckBox> m_lock;
};

// ----------------------------------------------------------------------------- sélection
class MarqueeTool : public Tool {
    Q_OBJECT
public:
    explicit MarqueeTool(bool ellipse) : m_ellipse(ellipse) {}
    QString id() const override { return m_ellipse ? "marquee_ellipse" : "marquee_rect"; }
    QString name() const override { return m_ellipse ? "Sélection elliptique (M)" : "Sélection rectangulaire (M)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_drag = false; }
    void buildOptions(OptionsBar&) override;
private:
    QRectF rectFor(const ToolEvent&) const;
    bool m_ellipse, m_drag = false; QPointF m_start; QRectF m_rect; Sel::Mode m_mode = Sel::Mode::Replace;
};

class LassoTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "lasso"; }
    QString name() const override { return "Lasso (L)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_poly.clear(); m_drag = false; }
    void buildOptions(OptionsBar&) override;
private:
    QPolygonF m_poly; bool m_drag = false; Sel::Mode m_mode = Sel::Mode::Replace;
};

class PolyLassoTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "lasso_poly"; }
    QString name() const override { return "Lasso polygonal (L)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void doubleClick(const ToolEvent&) override { close(); }
    bool keyPress(QKeyEvent*, CanvasView*) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_poly.clear(); }
    void deactivate() override { m_poly.clear(); }
    void buildOptions(OptionsBar&) override;
private:
    void close();
    QPolygonF m_poly; QPointF m_hover; Sel::Mode m_mode = Sel::Mode::Replace;
};

class WandTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "wand"; }
    QString name() const override { return "Baguette magique (W)"; }
    void press(const ToolEvent&) override;
    void buildOptions(OptionsBar&) override;
};

// Sélection par IA (MobileSAM, « segmentation par indication ») : un clic = objet sous le curseur, un cadre glissé = objet
// dans le cadre. Maj = ajouter, Alt = soustraire, comme les autres outils de sélection. L'image est analysée une fois
// (quelques secondes sur CPU) puis chaque clic est quasi instantané ; elle est ré-analysée automatiquement si le document a
// été modifié depuis (ou via « Ré-analyser l'image »).
class SamTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "sam"; }
    QString name() const override { return "Sélection par IA — MobileSAM (W)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void activate() override;
    void deactivate() override { m_drag = false; }
    void cancel() override { m_drag = false; }
    void buildOptions(OptionsBar&) override;
    void invalidate() { m_stale = true; }
private:
    bool ensureEncoded(Document*, CanvasView*);
    QPointer<Document> m_doc;
    bool m_stale = true, m_drag = false, m_lastSampleAll = true;
    QPointF m_a, m_b, m_wa, m_wb;
};

// ----------------------------------------------------------------------------- recadrage / pipette
class CropTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "crop"; }
    QString name() const override { return "Recadrage (C)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void doubleClick(const ToolEvent&) override { commit(); }
    bool keyPress(QKeyEvent*, CanvasView*) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_rect = QRectF(); m_h = 0; }
    void deactivate() override { m_rect = QRectF(); }
private:
    void commit();
    int hit(const QPointF& w, CanvasView*) const;
    QRectF m_rect; int m_h = 0; QPointF m_start; QRectF m_r0; Document* m_doc = nullptr;
};

class EyedropperTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "eyedropper"; }
    QString name() const override { return "Pipette (I)"; }
    void press(const ToolEvent& e) override { sample(e); }
    void move(const ToolEvent& e) override { if (e.buttons & Qt::LeftButton) sample(e); }
    void buildOptions(OptionsBar&) override;
    static QColor colorAt(Document*, QPointF p, int size, bool merged);
private:
    void sample(const ToolEvent&);
};

// ----------------------------------------------------------------------------- peinture
class PaintTool : public Tool {
    Q_OBJECT
public:
    enum Kind { Brush, Pencil, Eraser, Clone, Blur, Sharpen, Smudge, Dodge, Burn };
    explicit PaintTool(Kind k) : m_kind(k) {}
    QString id() const override;
    QString name() const override;
    QCursor cursor() const override { return Qt::BlankCursor; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    bool keyPress(QKeyEvent*, CanvasView*) override;
    void cancel() override { m_engine.cancel(); }
    void deactivate() override { m_engine.cancel(); }
    void buildOptions(OptionsBar&) override;
private:
    BrushEngine::Params params() const;
    Kind m_kind; BrushEngine m_engine;
    QPointF m_hover; bool m_hasHover = false;
    QPointF m_lastEnd; bool m_hasLast = false;
    QPoint m_cloneSrc; bool m_hasSrc = false; QPoint m_cloneOffset; bool m_offsetSet = false; QPointF m_cloneStart;
};

class BucketTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "bucket"; }
    QString name() const override { return "Pot de peinture (G)"; }
    void press(const ToolEvent&) override;
    void buildOptions(OptionsBar&) override;
};

class GradientTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "gradient"; }
    QString name() const override { return "Dégradé (G)"; }
    void press(const ToolEvent& e) override { if (e.button == Qt::LeftButton) { m_a = m_b = e.pos; m_drag = true; } }
    void move(const ToolEvent& e) override { if (m_drag) { m_b = e.pos; e.view->refresh(); } }
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_drag = false; }
    void buildOptions(OptionsBar&) override;
private:
    QPointF m_a, m_b; bool m_drag = false;
};

// ----------------------------------------------------------------------------- texte / formes / navigation
class TextTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "text"; }
    QString name() const override { return "Texte (T)"; }
    QCursor cursor() const override { return Qt::IBeamCursor; }
    void press(const ToolEvent&) override;
    void buildOptions(OptionsBar&) override;
};

class ShapeTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "shape"; }
    QString name() const override { return "Forme (U)"; }
    void press(const ToolEvent& e) override { if (e.button == Qt::LeftButton) { m_a = m_b = e.pos; m_drag = true; } }
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_drag = false; }
    void buildOptions(OptionsBar&) override;
private:
    QRectF rectFor(const ToolEvent&) const;
    QPointF m_a, m_b; QRectF m_rect; bool m_drag = false;
};

class HandTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "hand"; }
    QString name() const override { return "Main (H)"; }
    QCursor cursor() const override { return m_drag ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }
    void press(const ToolEvent& e) override { m_drag = true; m_last = e.widgetPos; }
    void move(const ToolEvent& e) override;
    void release(const ToolEvent&) override { m_drag = false; }
private:
    bool m_drag = false; QPointF m_last;
};

class ZoomTool : public Tool {
    Q_OBJECT
public:
    QString id() const override { return "zoom"; }
    QString name() const override { return "Zoom (Z)"; }
    void press(const ToolEvent&) override;
    void move(const ToolEvent&) override;
    void release(const ToolEvent&) override;
    void paintOverlay(QPainter&, CanvasView*) override;
    void cancel() override { m_drag = false; }
private:
    bool m_drag = false; QPointF m_a, m_b, m_wa, m_wb;
};
