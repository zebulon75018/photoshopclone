#include "EffectDialog.h"
#include "Widgets.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

EffectDialog::EffectDialog(Document* doc, EffectPtr e, const Params& initial, QWidget* parent)
    : MovableDialog(parent), m_doc(doc), m_effect(std::move(e)), m_params(initial), m_layer(doc->activeLayer()) {
    setWindowTitle(m_effect->name.section(QStringLiteral("…"), 0, 0));
    m_orig = m_layer->image;    // en-tête partagé : l'aperçu remplace `image` par une nouvelle matrice, jamais sur place
    m_timer.setSingleShot(true); m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, [this] { preview(); });
    buildUi();
    schedulePreview();
}

void EffectDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    QTabWidget* curveTabs = nullptr;
    for (const ParamDef& d : m_effect->defs) {
        const QString key = d.key;
        switch (d.type) {
        case ParamDef::Int: case ParamDef::Double: {
            auto* s = new SliderSpin(d.min, d.max, m_params.d(key), d.decimals);
            connect(s, &SliderSpin::valueChanged, this, [this, key, d](double v) { m_params.set(key, d.type == ParamDef::Int ? QVariant(int(std::lround(v))) : QVariant(v)); schedulePreview(); });
            m_resetters.push_back([s, d] { s->setValue(d.def.toDouble()); });
            form->addRow(d.label + " :", s);
            break;
        }
        case ParamDef::Bool: {
            auto* c = new QCheckBox(d.label); c->setChecked(m_params.b(key));
            connect(c, &QCheckBox::toggled, this, [this, key](bool v) { m_params.set(key, v); schedulePreview(); });
            m_resetters.push_back([c, d] { c->setChecked(d.def.toBool()); });
            form->addRow(c);
            break;
        }
        case ParamDef::Choice: {
            auto* c = new QComboBox; c->addItems(d.choices); c->setCurrentIndex(m_params.i(key));
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key](int v) { m_params.set(key, v); schedulePreview(); });
            m_resetters.push_back([c, d] { c->setCurrentIndex(d.def.toInt()); });
            form->addRow(d.label + " :", c);
            break;
        }
        case ParamDef::Color: {
            auto* b = new QPushButton;
            auto paint = [b](const QColor& c) { b->setStyleSheet(QString("background:%1;min-height:20px;").arg(c.name())); };
            paint(m_params.color(key));
            connect(b, &QPushButton::clicked, this, [this, key, paint] {
                QColor c = Dlg::pickColor(m_params.color(key), tr("Couleur"), this);
                if (c.isValid()) { m_params.set(key, c); paint(c); schedulePreview(); }
            });
            form->addRow(d.label + " :", b);
            break;
        }
        case ParamDef::Curve: {
            if (!curveTabs) { curveTabs = new QTabWidget; form->addRow(curveTabs); }
            auto* cw = new CurveWidget; cw->setPoints(m_params.curve(key));
            connect(cw, &CurveWidget::changed, this, [this, cw, key] { m_params.set(key, QVariant::fromValue(cw->points())); schedulePreview(); });
            m_resetters.push_back([cw, d] { cw->setPoints(d.def.value<QPolygonF>()); });
            curveTabs->addTab(cw, d.label);
            break;
        }
        }
    }
    root->addLayout(form);
    m_error = new QLabel;
    m_error->setWordWrap(true);
    m_error->setStyleSheet("color:#ff8a80;");
    m_error->hide();
    root->addWidget(m_error);
    auto* prev = new QCheckBox(tr("Aperçu")); prev->setChecked(true);
    connect(prev, &QCheckBox::toggled, this, [this](bool on) { m_preview = on; if (on) preview(); else restore(); });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    frenchButtons(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        m_params = m_effect->defaults();
        for (auto& r : m_resetters) r();
        schedulePreview();
    });
    auto* row = new QHBoxLayout; row->addWidget(prev); row->addStretch(); row->addWidget(bb);
    root->addLayout(row);
    setMinimumWidth(420);
}

void EffectDialog::schedulePreview() { if (m_preview) m_timer.start(); }

void EffectDialog::preview() {
    if (!m_preview) return;
    cv::Mat source = (m_effect->supportsSampleAllLayers && m_params.b("sampleAll")) ? m_doc->compositeCopy() : m_orig;
    EffectDiag::takeError();
    cv::Mat res = m_effect->run(source, m_doc->selection(), m_params);
    QString err = EffectDiag::takeError();
    m_error->setText(err);
    m_error->setVisible(!err.isEmpty());
    if (!err.isEmpty()) { m_layer->image = m_orig; m_doc->invalidateAll(); return; }   // échec : on montre l'original
    m_layer->image = blendWithSelection(m_orig, res, m_doc->selection());
    m_doc->invalidateAll();
}

void EffectDialog::restore() {
    m_timer.stop();
    m_layer->image = m_orig;
    m_doc->invalidateAll();
}

void EffectDialog::accept() { restore(); QDialog::accept(); }
void EffectDialog::reject() { restore(); QDialog::reject(); }
