#pragma once
#include <QComboBox>
#include <QListWidget>
#include <QTimer>
#include <QUndoView>
#include <QWidget>
#include "core/Document.h"

class SliderSpin; class QCheckBox; class QToolButton; class QLineEdit;

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget* parent = nullptr);
    void setDocument(Document* d);
private:
    void rebuild();
    void updateThumbs();
    void syncControls();
    void onReordered();
    void contextMenu(const QPoint&);
    void rename(QListWidgetItem*);
    Layer::Ptr layerOf(QListWidgetItem*) const;

    Document* m_doc = nullptr;
    QListWidget* m_list; QComboBox* m_blend; SliderSpin* m_opacity; QCheckBox* m_lock; QToolButton* m_editMask;
    QTimer m_thumbTimer, m_commitTimer;
    bool m_building = false, m_hasBefore = false;
    LayerState m_before;
};

class HistoryPanel : public QUndoView {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget* parent = nullptr) : QUndoView(parent) { setEmptyLabel("État initial"); }
    void setDocument(Document* d) { setStack(d ? d->undoStack() : nullptr); }
};

class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);
private:
    void sync();
    QComboBox* m_target; SliderSpin *m_r, *m_g, *m_b; QLineEdit* m_hex; bool m_busy = false;
};

class SwatchesPanel : public QWidget {
    Q_OBJECT
public:
    explicit SwatchesPanel(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(12 * 20 + 8, 7 * 20 + 8); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    std::vector<QColor> m_colors;
};

class HistogramPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistogramPanel(QWidget* parent = nullptr);
    void setDocument(Document* d);
    QSize sizeHint() const override { return QSize(260, 130); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void compute();
    Document* m_doc = nullptr; QComboBox* m_channel; QTimer m_timer; std::vector<float> m_h[4];
};
