#include "Dialogs.h"
#include <opencv2/imgproc.hpp>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

static QDialogButtonBox* buttons(QDialog* d) {
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
    frenchButtons(b);
    return b;
}
static QSpinBox* spin(int mn, int mx, int v) { auto* s = new QSpinBox; s->setRange(mn, mx); s->setValue(v); s->setSuffix(" px"); return s; }

// ------------------------------------------------------------------------------------------ Nouveau document
NewDocumentDialog::NewDocumentDialog(QWidget* parent, QSize sug) : MovableDialog(parent) {
    setWindowTitle("Nouveau document");
    auto* f = new QFormLayout(this);
    m_preset = new QComboBox;
    struct P { const char* n; int w, h; };
    static const P presets[] = {{"Personnalisé", 0, 0}, {"Web 1920 × 1080", 1920, 1080}, {"HD 1280 × 720", 1280, 720}, {"4K 3840 × 2160", 3840, 2160},
                                {"A4 300 ppp 2480 × 3508", 2480, 3508}, {"Carré 1080 × 1080", 1080, 1080}, {"Icône 512 × 512", 512, 512}};
    for (auto& p : presets) m_preset->addItem(p.n, QSize(p.w, p.h));
    m_w = spin(1, 30000, sug.isValid() ? sug.width() : 1920);
    m_h = spin(1, 30000, sug.isValid() ? sug.height() : 1080);
    m_bg = new QComboBox; m_bg->addItems({"Blanc", "Couleur d'arrière-plan", "Transparent"});
    f->addRow("Présélection :", m_preset); f->addRow("Largeur :", m_w); f->addRow("Hauteur :", m_h); f->addRow("Contenu de l'arrière-plan :", m_bg);
    f->addRow(buttons(this));
    connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        QSize s = m_preset->currentData().toSize();
        if (s.width() > 0) { m_w->setValue(s.width()); m_h->setValue(s.height()); }
    });
}
QSize NewDocumentDialog::docSize() const { return QSize(m_w->value(), m_h->value()); }
int NewDocumentDialog::background() const { return m_bg->currentIndex(); }

// ------------------------------------------------------------------------------------------ Taille de l'image
ImageSizeDialog::ImageSizeDialog(QWidget* parent, QSize cur) : MovableDialog(parent), m_ratio(double(cur.width()) / cur.height()) {
    setWindowTitle("Taille de l'image");
    auto* f = new QFormLayout(this);
    m_w = spin(1, 30000, cur.width()); m_h = spin(1, 30000, cur.height());
    m_lock = new QCheckBox("Conserver les proportions"); m_lock->setChecked(true);
    m_interp = new QComboBox; m_interp->addItems({"Bicubique (idéal pour l'agrandissement)", "Bilinéaire", "Lanczos", "Au plus proche (pixels durs)"});
    f->addRow("Largeur :", m_w); f->addRow("Hauteur :", m_h); f->addRow(m_lock); f->addRow("Rééchantillonnage :", m_interp); f->addRow(buttons(this));
    connect(m_w, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { if (m_lock->isChecked() && !m_busy) { m_busy = true; m_h->setValue(std::max(1, int(std::lround(v / m_ratio)))); m_busy = false; } });
    connect(m_h, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { if (m_lock->isChecked() && !m_busy) { m_busy = true; m_w->setValue(std::max(1, int(std::lround(v * m_ratio)))); m_busy = false; } });
}
QSize ImageSizeDialog::newSize() const { return QSize(m_w->value(), m_h->value()); }
int ImageSizeDialog::interpolation() const {
    static const int v[] = {cv::INTER_CUBIC, cv::INTER_LINEAR, cv::INTER_LANCZOS4, cv::INTER_NEAREST};
    return v[m_interp->currentIndex()];
}

// ------------------------------------------------------------------------------------------ Taille de la zone de travail
CanvasSizeDialog::CanvasSizeDialog(QWidget* parent, QSize cur) : MovableDialog(parent) {
    setWindowTitle("Taille de la zone de travail");
    auto* f = new QFormLayout(this);
    m_w = spin(1, 30000, cur.width()); m_h = spin(1, 30000, cur.height());
    m_anchor = new QComboBox;
    m_anchor->addItems({"Haut gauche", "Haut centre", "Haut droite", "Milieu gauche", "Centre", "Milieu droite", "Bas gauche", "Bas centre", "Bas droite"});
    m_anchor->setCurrentIndex(4);
    f->addRow("Largeur :", m_w); f->addRow("Hauteur :", m_h); f->addRow("Ancrage :", m_anchor); f->addRow(buttons(this));
}
QSize CanvasSizeDialog::newSize() const { return QSize(m_w->value(), m_h->value()); }
int CanvasSizeDialog::anchor() const { return m_anchor->currentIndex(); }

// ------------------------------------------------------------------------------------------ Texte
TextDialog::TextDialog(QWidget* parent, const TextData& td) : MovableDialog(parent), m_col(td.color), m_td(td) {
    setWindowTitle("Texte");
    setMinimumSize(460, 330);
    auto* v = new QVBoxLayout(this);
    m_text = new QPlainTextEdit(td.text);
    m_text->setPlaceholderText("Saisissez votre texte…");
    auto* f = new QFormLayout;
    m_font = new QFontComboBox; m_font->setCurrentFont(QFont(td.family));
    m_size = spin(4, 2000, td.pixelSize);
    m_b = new QCheckBox("Gras"); m_b->setChecked(td.bold);
    m_i = new QCheckBox("Italique"); m_i->setChecked(td.italic);
    m_u = new QCheckBox("Souligné"); m_u->setChecked(td.underline);
    m_align = new QComboBox; m_align->addItems({"Gauche", "Centré", "Droite"}); m_align->setCurrentIndex(td.align);
    m_color = new QPushButton;
    auto paint = [this] { m_color->setStyleSheet(QString("background:%1;min-height:18px;").arg(m_col.name())); };
    paint();
    connect(m_color, &QPushButton::clicked, this, [this, paint] { QColor c = Dlg::pickColor(m_col, "Couleur du texte", this); if (c.isValid()) { m_col = c; paint(); } });
    auto* style = new QWidget; auto* sl = new QHBoxLayout(style); sl->setContentsMargins(0, 0, 0, 0); sl->addWidget(m_b); sl->addWidget(m_i); sl->addWidget(m_u);
    f->addRow("Police :", m_font); f->addRow("Corps :", m_size); f->addRow("Style :", style); f->addRow("Alignement :", m_align); f->addRow("Couleur :", m_color);
    v->addWidget(m_text, 1); v->addLayout(f); v->addWidget(buttons(this));
    m_text->setFocus();
}

TextData TextDialog::result() const {
    TextData t = m_td;
    t.text = m_text->toPlainText(); t.family = m_font->currentFont().family(); t.pixelSize = m_size->value();
    t.bold = m_b->isChecked(); t.italic = m_i->isChecked(); t.underline = m_u->isChecked(); t.align = m_align->currentIndex(); t.color = m_col;
    return t;
}

bool TextDialog::edit(QWidget* parent, TextData& td) {
    TextDialog d(parent, td);
    if (d.exec() != QDialog::Accepted) return false;
    td = d.result();
    return true;
}
