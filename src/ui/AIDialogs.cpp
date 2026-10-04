#include "AIDialogs.h"
#include "Widgets.h"
#include "ai/AIModels.h"
#include "ai/MaskRefine.h"
#include "core/MatUtil.h"
#include <opencv2/imgproc.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QDialogButtonBox* okCancel(QDialog* d, QPushButton** okOut = nullptr) {
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    frenchButtons(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, d, &QDialog::reject);
    if (okOut) *okOut = bb->button(QDialogButtonBox::Ok);
    return bb;
}
QLabel* statusLabel() {
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setMinimumHeight(34);
    l->setTextFormat(Qt::PlainText);
    return l;
}
void setStatus(QLabel* l, const QString& text, bool error) {
    l->setStyleSheet(error ? "color:#ff8a80;" : "color:#b0b0b0;");
    l->setText(text);
}
cv::Mat sourceFor(Document* d, const Layer::Ptr& l, const cv::Mat& layerImage, bool sampleAll) {
    return sampleAll ? d->compositeCopy() : layerImage;
}
}

// ============================================================================================ réglages des modèles
AIModelsDialog::AIModelsDialog(QWidget* parent) : MovableDialog(parent) {
    setWindowTitle(tr("Modèles IA (vision.cpp)"));
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Les fonctions IA utilisent des fichiers de modèle <b>.gguf</b> (plusieurs dizaines à centaines de Mo) qui ne sont pas "
        "fournis avec PhotoClone. Téléchargez-les via les liens ci-dessous, puis indiquez leur emplacement. Rien n'est envoyé sur Internet : "
        "tout le calcul se fait localement (CPU)."));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    root->addWidget(intro);
    if (!AIModels::libraryAvailable()) {
        auto* warn = new QLabel(tr("⚠ Cette version de PhotoClone a été compilée SANS la bibliothèque vision.cpp : les modèles ne pourront pas être utilisés (voir depend/visioncpp/BUILD_FROM_SOURCE.md)."));
        warn->setWordWrap(true);
        warn->setStyleSheet("color:#ffcc80;");
        root->addWidget(warn);
    }
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int r = 0;
    for (const auto& info : AIModels::all()) {
        auto* title = new QLabel(QString("<b>%1</b><br><span style='color:#aaa'>%2</span>").arg(info.name, info.task));
        title->setTextFormat(Qt::RichText);
        auto* edit = new QLineEdit(AIModels::modelPath(info.id));
        edit->setPlaceholderText(tr("ex. %1").arg(info.suggestedFile));
        edit->setMinimumWidth(320);
        auto* browse = new QPushButton(tr("Parcourir…"));
        auto* clear = new QPushButton(tr("Effacer"));
        auto* link = new QLabel(tr("<a href=\"%1\">Télécharger un modèle ↗</a>").arg(info.downloadUrl));
        link->setTextFormat(Qt::RichText);
        link->setOpenExternalLinks(true);
        link->setAlignment(Qt::AlignRight | Qt::AlignTop);
        auto* status = new QLabel;
        status->setWordWrap(true);
        status->setMinimumHeight(40);                       // 2 lignes : les messages d'erreur (chemin + raison) ne sont plus coupés
        status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_rows.push_back({info.id, edit, status});
        const size_t idx = m_rows.size() - 1;
        connect(browse, &QPushButton::clicked, this, [this, idx, edit] {
            QString f = QFileDialog::getOpenFileName(this, "Choisir un modèle .gguf", edit->text(), tr("Modèles GGUF (*.gguf);;Tous les fichiers (*)"));
            if (!f.isEmpty()) { edit->setText(f); refreshRow(m_rows[idx]); }
        });
        connect(clear, &QPushButton::clicked, this, [this, idx, edit] { edit->clear(); refreshRow(m_rows[idx]); });
        connect(edit, &QLineEdit::editingFinished, this, [this, idx] { refreshRow(m_rows[idx]); });
        grid->addWidget(title, r, 0, 2, 1, Qt::AlignTop);
        grid->addWidget(edit, r, 1);
        grid->addWidget(browse, r, 2);
        grid->addWidget(clear, r, 3);
        auto* sub = new QHBoxLayout;
        sub->addWidget(status, 1);
        sub->addWidget(link);
        grid->addLayout(sub, r + 1, 1, 1, 3);
        r += 2;
        refreshRow(m_rows.back());
    }
    root->addLayout(grid);
    root->addWidget(okCancel(this));
    grid->setVerticalSpacing(6);
    setMinimumWidth(820);
}

void AIModelsDialog::refreshRow(Row& row) {
    const QString path = row.path->text().trimmed();
    if (path.isEmpty()) { setStatus(row.status, tr("Non configuré."), false); return; }
    QString bad = AIBackend::validateModelFile(row.arch, path);
    if (!bad.isEmpty()) { setStatus(row.status, "✗ " + bad, true); return; }
    setStatus(row.status, tr("✓ Fichier valide (%1 Mo).").arg(QFileInfo(path).size() / (1024 * 1024)), false);
}

void AIModelsDialog::accept() {
    for (auto& row : m_rows) AIModels::setModelPath(row.arch, row.path->text().trimmed());
    AIBackend::clearCache();    // les chemins ont pu changer : les modèles seront rechargés à la demande
    QDialog::accept();
}

// ============================================================================================ suppression de l'arrière-plan
RemoveBackgroundDialog::RemoveBackgroundDialog(Document* doc, QWidget* parent, AIImageFn provider)
    : MovableDialog(parent), m_doc(doc), m_layer(doc->activeLayer()), m_provider(std::move(provider)) {
    setWindowTitle(tr("Supprimer l'arrière-plan (IA)"));
    if (!m_provider) m_provider = [](const cv::Mat& m) { return AIBackend::segmentDichotomous(m); };
    m_orig = m_layer->image;   // en-tête partagé : l'aperçu remplace `image`, jamais de modification sur place
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, [this] { preview(); });

    auto* root = new QVBoxLayout(this);

    auto* src = new QGroupBox(tr("Analyse (modèle BiRefNet)"));
    auto* sl = new QHBoxLayout(src);
    m_sampleAll = new QCheckBox(tr("Analyser l'image fusionnée (tous les calques visibles)"));
    m_recompute = new QPushButton(tr("Recalculer"));
    m_recompute->setToolTip(tr("Relance le réseau de neurones (peut prendre plusieurs secondes sur CPU)."));
    sl->addWidget(m_sampleAll, 1);
    sl->addWidget(m_recompute);
    root->addWidget(src);

    auto* mk = new QGroupBox(tr("Affinage du masque (aperçu instantané)"));
    auto* mf = new QFormLayout(mk);
    m_threshold = new SliderSpin(0, 255, 0);
    m_threshold->setToolTip(tr("0 = bords doux (dégradé) ; sinon seuil dur : pixels ≥ seuil = sujet."));
    m_shift = new SliderSpin(-50, 50, 0);
    m_feather = new SliderSpin(0, 50, 0);
    m_keepLargest = new QCheckBox(tr("Ne garder que le plus grand sujet (supprime les îlots)"));
    m_invert = new QCheckBox(tr("Inverser (garder l'arrière-plan, supprimer le sujet)"));
    mf->addRow(tr("Seuil (0 = bords doux) :"), m_threshold);
    mf->addRow(tr("Contracter (−) / étendre (+) px :"), m_shift);
    mf->addRow(tr("Contour progressif (px) :"), m_feather);
    mf->addRow(m_keepLargest);
    mf->addRow(m_invert);
    root->addWidget(mk);

    auto* out = new QGroupBox(tr("Résultat"));
    auto* of = new QFormLayout(out);
    m_output = new QComboBox;
    m_output->addItems({tr("Masque de fusion sur le calque actif (non destructif)"), tr("Nouveau calque : sujet seul (fond transparent)"),
                        tr("Remplacer le calque actif (fond rendu transparent)"), tr("Sélection uniquement")});
    m_defringe = new QCheckBox(tr("Corriger les couleurs de bord (supprime le liseré de l'ancien fond)"));
    m_defringe->setChecked(true);
    m_defringeRadius = new SliderSpin(5, 100, 30);
    of->addRow(tr("Sortie :"), m_output);
    of->addRow(m_defringe);
    of->addRow(tr("Rayon de correction (px) :"), m_defringeRadius);
    root->addWidget(out);

    m_status = statusLabel();
    root->addWidget(m_status);
    m_previewCb = new QCheckBox(tr("Aperçu"));
    m_previewCb->setChecked(true);
    auto* row = new QHBoxLayout;
    row->addWidget(m_previewCb);
    row->addStretch();
    row->addWidget(okCancel(this, &m_ok));
    root->addLayout(row);
    setMinimumWidth(560);

    for (SliderSpin* s : {m_threshold, m_shift, m_feather, m_defringeRadius}) connect(s, &SliderSpin::valueChanged, this, [this] { schedulePreview(); });
    for (QCheckBox* c : {m_keepLargest, m_invert}) connect(c, &QCheckBox::toggled, this, [this] { schedulePreview(); });
    connect(m_defringe, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    connect(m_output, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateEnabled(); schedulePreview(); });
    connect(m_previewCb, &QCheckBox::toggled, this, [this](bool on) { m_preview = on; if (on) preview(); else restore(); });
    connect(m_sampleAll, &QCheckBox::toggled, this, [this] { m_recompute->setText(tr("Recalculer (l'analyse a changé)")); });
    connect(m_recompute, &QPushButton::clicked, this, [this] { compute(); });
    updateEnabled();
    m_ok->setEnabled(false);
    QTimer::singleShot(0, this, [this] { compute(); });   // analyse initiale dès l'ouverture
}

void RemoveBackgroundDialog::updateEnabled() {
    const int mode = m_output->currentIndex();
    const bool pixels = mode == 1 || mode == 2;     // seuls ces modes modifient des couleurs
    m_defringe->setEnabled(pixels);
    m_defringeRadius->setEnabled(pixels && m_defringe->isChecked());
    m_ok->setEnabled(!m_raw.empty());
}

Ops::RemoveBgParams RemoveBackgroundDialog::params() const {
    Ops::RemoveBgParams p;
    p.mask.threshold = int(m_threshold->value());
    p.mask.shift = int(m_shift->value());
    p.mask.feather = int(m_feather->value());
    p.mask.keepLargest = m_keepLargest->isChecked();
    p.mask.invert = m_invert->isChecked();
    p.output = Ops::RemoveBgParams::Output(m_output->currentIndex());
    p.defringe = m_defringe->isChecked();
    p.defringeRadius = int(m_defringeRadius->value());
    p.sampleAll = m_sampleAll->isChecked();
    return p;
}

void RemoveBackgroundDialog::compute() {
    restore();
    setStatus(m_status, tr("Analyse en cours… (peut prendre plusieurs secondes sur CPU ; l'interface est figée pendant le calcul)"), false);
    m_ok->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    cv::Mat source = sourceFor(m_doc, m_layer, m_orig, m_sampleAll->isChecked());
    AIBackend::Result r = m_provider(source);
    QApplication::restoreOverrideCursor();
    m_recompute->setText(tr("Recalculer"));
    if (!r.ok) {
        m_raw = cv::Mat();
        setStatus(m_status, r.error.isEmpty() ? tr("Échec de l'analyse.") : r.error, true);
    } else if (r.data.type() != CV_8UC1 || r.data.size() != source.size()) {
        m_raw = cv::Mat();
        setStatus(m_status, tr("Le modèle a renvoyé un masque de taille inattendue."), true);
    } else {
        m_raw = r.data;
        setStatus(m_status, tr("Masque calculé : ajustez les réglages ci-dessus (aperçu instantané), puis validez."), false);
    }
    updateEnabled();
    preview();
}

void RemoveBackgroundDialog::schedulePreview() { if (m_preview) m_timer->start(); }

void RemoveBackgroundDialog::preview() {
    if (!m_preview || m_raw.empty() || m_output->currentIndex() == 3) { if (m_raw.empty() || m_output->currentIndex() == 3) restore(); return; }
    cv::Mat refined = refineMask(m_raw, params().mask);
    m_layer->image = applyMaskToAlpha(m_orig, refined);    // aperçu simple (les couleurs de bord sont corrigées à la validation)
    m_doc->invalidateAll();
}

void RemoveBackgroundDialog::restore() {
    m_timer->stop();
    if (m_layer->image.data != m_orig.data) { m_layer->image = m_orig; m_doc->invalidateAll(); }
}

void RemoveBackgroundDialog::accept() { restore(); QDialog::accept(); }
void RemoveBackgroundDialog::reject() { restore(); QDialog::reject(); }

// ============================================================================================ carte de profondeur
DepthDialog::DepthDialog(Document* doc, QWidget* parent, AIImageFn provider) : MovableDialog(parent), m_doc(doc), m_provider(std::move(provider)) {
    setWindowTitle(tr("Carte de profondeur (IA)"));
    if (!m_provider) m_provider = [](const cv::Mat& m) { return AIBackend::estimateDepth(m); };
    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    m_sampleAll = new QCheckBox(tr("Analyser l'image fusionnée (tous les calques)"));
    m_sampleAll->setChecked(true);
    m_recompute = new QPushButton(tr("Recalculer"));
    top->addWidget(m_sampleAll, 1);
    top->addWidget(m_recompute);
    root->addLayout(top);

    m_thumb = new QLabel;
    m_thumb->setFixedSize(300, 190);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setStyleSheet("background:#1e1e1e;border:1px solid #555;");
    root->addWidget(m_thumb, 0, Qt::AlignHCenter);

    auto* f = new QFormLayout;
    m_invert = new QCheckBox(tr("Inverser (proche ↔ loin)"));
    m_auto = new QCheckBox(tr("Étirer automatiquement le contraste"));
    m_auto->setChecked(true);
    m_smooth = new SliderSpin(0, 30, 0);
    f->addRow(m_invert);
    f->addRow(m_auto);
    f->addRow(tr("Lissage (px) :"), m_smooth);
    root->addLayout(f);

    auto* out = new QGroupBox(tr("Créer"));
    auto* of = new QFormLayout(out);
    m_makeLayer = new QCheckBox(tr("Un calque « Profondeur » en niveaux de gris"));
    m_makeLayer->setChecked(true);
    m_makeSel = new QCheckBox(tr("Une sélection par seuil de profondeur"));
    m_selMode = new QComboBox;
    m_selMode->addItems({tr("Zones claires (≥ seuil)"), tr("Zones sombres (≤ seuil)")});
    m_threshold = new SliderSpin(1, 254, 128);
    m_feather = new SliderSpin(0, 50, 0);
    of->addRow(m_makeLayer);
    of->addRow(m_makeSel);
    of->addRow(tr("Sélectionner :"), m_selMode);
    of->addRow(tr("Seuil :"), m_threshold);
    of->addRow(tr("Contour progressif (px) :"), m_feather);
    root->addWidget(out);

    m_status = statusLabel();
    root->addWidget(m_status);
    root->addWidget(okCancel(this, &m_ok));
    setMinimumWidth(460);

    for (QCheckBox* c : {m_invert, m_auto, m_makeSel, m_makeLayer}) connect(c, &QCheckBox::toggled, this, [this] { updateEnabled(); refreshThumb(); });
    for (SliderSpin* s : {m_smooth, m_threshold, m_feather}) connect(s, &SliderSpin::valueChanged, this, [this] { refreshThumb(); });
    connect(m_selMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshThumb(); });
    connect(m_recompute, &QPushButton::clicked, this, [this] { compute(); });
    m_ok->setEnabled(false);
    updateEnabled();
    QTimer::singleShot(0, this, [this] { compute(); });
}

void DepthDialog::updateEnabled() {
    for (QWidget* w : std::initializer_list<QWidget*>{m_selMode, m_threshold, m_feather}) w->setEnabled(m_makeSel->isChecked());
    m_ok->setEnabled(!m_raw.empty() && (m_makeLayer->isChecked() || m_makeSel->isChecked()));
}

Ops::DepthApplyParams DepthDialog::params() const {
    Ops::DepthApplyParams p;
    p.depth.invert = m_invert->isChecked();
    p.depth.autoLevels = m_auto->isChecked();
    p.depth.smooth = int(m_smooth->value());
    p.makeLayer = m_makeLayer->isChecked();
    p.makeSelection = m_makeSel->isChecked();
    p.threshold = int(m_threshold->value());
    p.selectBright = m_selMode->currentIndex() == 0;
    p.feather = int(m_feather->value());
    return p;
}

void DepthDialog::compute() {
    setStatus(m_status, tr("Analyse en cours… (l'interface est figée pendant le calcul)"), false);
    m_ok->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    cv::Mat source = m_sampleAll->isChecked() ? m_doc->compositeCopy() : m_doc->activeLayer()->image;
    AIBackend::Result r = m_provider(source);
    QApplication::restoreOverrideCursor();
    if (!r.ok) { m_raw = cv::Mat(); setStatus(m_status, r.error.isEmpty() ? tr("Échec de l'analyse.") : r.error, true); }
    else if (r.data.type() != CV_8UC1 || r.data.size() != source.size()) { m_raw = cv::Mat(); setStatus(m_status, tr("Le modèle a renvoyé une carte de taille inattendue."), true); }
    else { m_raw = r.data; setStatus(m_status, tr("Carte calculée. Astuce : si les objets proches apparaissent sombres, cochez « Inverser »."), false); }
    updateEnabled();
    refreshThumb();
}

void DepthDialog::refreshThumb() {
    if (m_raw.empty()) { m_thumb->clear(); return; }
    Ops::DepthApplyParams p = params();
    cv::Mat depth = refineDepth(m_raw, p.depth);
    cv::Mat bgra;
    cv::cvtColor(depth, bgra, cv::COLOR_GRAY2BGRA);
    if (p.makeSelection) {
        cv::Mat sel = depthToSelection(depth, p.threshold, p.selectBright, p.feather);
        if (!sel.empty()) {
            cv::Mat tint(bgra.size(), CV_8UC4, cv::Scalar(60, 60, 255, 255));    // rouge (BGRA)
            cv::Mat blended;
            cv::addWeighted(bgra, 0.55, tint, 0.45, 0, blended);
            blended.copyTo(bgra, sel);
        }
    }
    cv::insertChannel(cv::Mat(bgra.size(), CV_8UC1, cv::Scalar(255)), bgra, 3);
    m_thumb->setPixmap(QPixmap::fromImage(mu::thumbnail(bgra, m_thumb->size() - QSize(4, 4), false)));
}

// ============================================================================================ agrandissement IA
UpscaleDialog::UpscaleDialog(Document* doc, QWidget* parent) : MovableDialog(parent), m_doc(doc) {
    setWindowTitle(tr("Agrandissement IA (Real-ESRGAN)"));
    auto* root = new QVBoxLayout(this);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    root->addWidget(m_info);
    auto* f = new QFormLayout;
    m_native = new QCheckBox(tr("Facteur natif du modèle (généralement ×4)"));
    m_native->setChecked(true);
    m_factor = new QDoubleSpinBox;
    m_factor->setRange(1.0, 8.0);
    m_factor->setSingleStep(0.25);
    m_factor->setValue(2.0);
    m_factor->setPrefix(tr("× "));
    f->addRow(m_native);
    f->addRow(tr("Facteur final personnalisé :"), m_factor);
    root->addLayout(f);
    auto* note = new QLabel(tr("Le modèle agrandit d'abord par son facteur natif ; un facteur personnalisé redimensionne ensuite le résultat (réduction propre si inférieur). "
                            "Tous les calques et masques du document sont agrandis (une passe par calque) ; les calques texte sont pixellisés."));
    note->setWordWrap(true);
    note->setStyleSheet("color:#aaa;");
    root->addWidget(note);
    root->addWidget(okCancel(this));
    setMinimumWidth(480);
    connect(m_native, &QCheckBox::toggled, this, [this] { refreshInfo(); });
    connect(m_factor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { refreshInfo(); });
    refreshInfo();
}

Ops::UpscaleParams UpscaleDialog::params() const {
    Ops::UpscaleParams p;
    p.nativeScale = m_native->isChecked();
    p.finalScale = m_factor->value();
    return p;
}

void UpscaleDialog::refreshInfo() {
    m_factor->setEnabled(!m_native->isChecked());
    const int n = int(m_doc->layers().size());
    const QSize s = m_doc->size();
    QString txt = tr("Document actuel : %1 × %2 px, %3 calque(s) → %3 passe(s) du modèle.").arg(s.width()).arg(s.height()).arg(n);
    const double fs = m_native->isChecked() ? 4.0 : m_factor->value();
    const double mp = double(s.width()) * fs * double(s.height()) * fs / 1e6;
    txt += tr("\nTaille estimée du résultat (×%1) : %2 × %3 px (%4 Mpx).").arg(fs).arg(int(s.width() * fs)).arg(int(s.height() * fs)).arg(mp, 0, 'f', 1);
    if (mp > 64) txt += tr("\n⚠ Au-delà de 64 Mpx l'opération sera refusée (limite de sécurité mémoire).");
    m_info->setText(txt);
}
