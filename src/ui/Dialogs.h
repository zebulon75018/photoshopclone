#pragma once
#include "MovableDialog.h"
#include <QSize>
#include "core/Layer.h"

class QSpinBox; class QCheckBox; class QComboBox; class QPlainTextEdit; class QFontComboBox; class QPushButton;

class NewDocumentDialog : public MovableDialog {
    Q_OBJECT
public:
    explicit NewDocumentDialog(QWidget* parent, QSize suggested = QSize());
    QSize docSize() const;
    int background() const;      // 0 blanc, 1 couleur d'arrière-plan, 2 transparent
private:
    QSpinBox *m_w, *m_h; QComboBox *m_preset, *m_bg;
};

class ImageSizeDialog : public MovableDialog {
    Q_OBJECT
public:
    ImageSizeDialog(QWidget* parent, QSize current);
    QSize newSize() const;
    int interpolation() const;   // constante cv::INTER_*
private:
    QSpinBox *m_w, *m_h; QCheckBox* m_lock; QComboBox* m_interp; double m_ratio; bool m_busy = false;
};

class CanvasSizeDialog : public MovableDialog {
    Q_OBJECT
public:
    CanvasSizeDialog(QWidget* parent, QSize current);
    QSize newSize() const;
    int anchor() const;
private:
    QSpinBox *m_w, *m_h; QComboBox* m_anchor;
};

class TextDialog : public MovableDialog {
    Q_OBJECT
public:
    static bool edit(QWidget* parent, TextData& td);   // false si annulé
private:
    explicit TextDialog(QWidget* parent, const TextData& td);
    TextData result() const;
    QPlainTextEdit* m_text; QFontComboBox* m_font; QSpinBox* m_size; QCheckBox *m_b, *m_i, *m_u; QComboBox* m_align; QPushButton* m_color; QColor m_col; TextData m_td;
};
