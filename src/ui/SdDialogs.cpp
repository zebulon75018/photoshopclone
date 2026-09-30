#include "SdDialogs.h"
#include <QDebug>
#include "Widgets.h"
#include "core/MatUtil.h"
#include "core/Workspace.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "ai/SdModelDiagnostics.h"
#include <cmath>

namespace {
QSettings uiSettings() { return QSettings("PhotoClone", "PhotoClone"); }
void setStatusStyle(QLabel* l, const QString& text, bool error) {
    l->setStyleSheet(error ? "color:#ff8a80;" : "color:#b0b0b0;");
    l->setText(text);
}
QPushButton* button(const QString& text) {
    auto* b = new QPushButton(text);
    b->setAutoDefault(false);
    b->setDefault(false);
    return b;
}
}

// ============================================================================================ diagnostic d'un fichier de modèle
ModelDiagnosticDialog::ModelDiagnosticDialog(const QString& path, QWidget* parent) : QDialog(parent), m_path(path) {
    setWindowTitle("Diagnostic du fichier de modèle");
    setModal(true);
    auto* root = new QVBoxLayout(this);
    auto* pathLbl = new QLabel(path);
    pathLbl->setWordWrap(true);
    pathLbl->setStyleSheet("color:#b0b0b0;");
    root->addWidget(pathLbl);

    m_text = new QPlainTextEdit;
    m_text->setReadOnly(true);
    m_text->setMinimumSize(560, 420);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    m_text->setFont(mono);
    root->addWidget(m_text);

    auto* row = new QHBoxLayout;
    m_shaButton = button("Calculer aussi le SHA-256 (relit tout le fichier, peut prendre du temps)");
    auto* copyBtn = button("Copier le rapport");
    auto* closeBtn = button("Fermer");
    closeBtn->setDefault(true);
    row->addWidget(m_shaButton);
    row->addStretch();
    row->addWidget(copyBtn);
    row->addWidget(closeBtn);
    root->addLayout(row);

    connect(m_shaButton, &QPushButton::clicked, this, [this] { refresh(true); });
    connect(copyBtn, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_text->toPlainText()); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    refresh(false);
}

void ModelDiagnosticDialog::refresh(bool withSha256) {
    if (withSha256) { m_shaButton->setEnabled(false); m_shaButton->setText("Calcul du SHA-256 en cours…"); QApplication::setOverrideCursor(Qt::WaitCursor); }
    const Sd::ModelDiagnostic diag = Sd::diagnoseModelFile(m_path, withSha256);
    if (withSha256) QApplication::restoreOverrideCursor();
    m_text->setPlainText(Sd::formatDiagnostic(diag));
    if (withSha256) m_shaButton->hide();
}

// ============================================================================================ réglages
SdSettingsDialog::Row SdSettingsDialog::makeRow(QGridLayout* grid, int r, const QString& key, const QString& label, const QString& value, const QString& tooltip) {
    auto* edit = new QLineEdit(value);
    edit->setMinimumWidth(380);
    if (!tooltip.isEmpty()) edit->setToolTip(tooltip);
    auto* browse = button("Parcourir…");
    auto* diag = new QToolButton;
    diag->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation));
    diag->setToolTip(QString("Diagnostiquer ce fichier (%1) : format détecté, cohérence, aperçu du contenu, architecture probable.").arg(label));
    diag->setAutoRaise(false);
    auto* clear = button("Effacer");
    auto* status = new QLabel;
    status->setObjectName("SdRowStatus_" + key);
    status->setWordWrap(true);
    status->setMinimumHeight(20);
    Row row{key, edit, status};
    m_rows.push_back(row);
    const size_t idx = m_rows.size() - 1;
    connect(browse, &QPushButton::clicked, this, [this, idx] {
        QString p = QFileDialog::getOpenFileName(this, "Choisir un fichier de modèle", m_rows[idx].edit->text(),
                                                 "Modèles (*.safetensors *.gguf *.ckpt *.pt);;Tous les fichiers (*)");
        if (!p.isEmpty()) { m_rows[idx].edit->setText(p); refreshRow(m_rows[idx]); refreshGlobalStatus(); }
    });
    connect(diag, &QToolButton::clicked, this, [this, idx] {
        const QString p = m_rows[idx].edit->text().trimmed();
        if (p.isEmpty()) { setStatusStyle(m_rows[idx].status, "Renseignez d'abord un chemin de fichier.", true); return; }
        ModelDiagnosticDialog(p, this).exec();
    });
    connect(clear, &QPushButton::clicked, this, [this, idx] { m_rows[idx].edit->clear(); refreshRow(m_rows[idx]); refreshGlobalStatus(); });
    connect(edit, &QLineEdit::editingFinished, this, [this, idx] { refreshRow(m_rows[idx]); refreshGlobalStatus(); });
    grid->addWidget(new QLabel("<b>" + label + "</b>"), r, 0);
    grid->addWidget(edit, r, 1);
    grid->addWidget(browse, r, 2);
    grid->addWidget(diag, r, 3);
    grid->addWidget(clear, r, 4);
    grid->addWidget(status, r + 1, 1, 1, 4);
    refreshRow(m_rows.back());
    return row;
}

SdSettingsDialog::SdSettingsDialog(QWidget* parent) : MovableDialog(parent) {
    setWindowTitle("Réglages de Stable Diffusion");
    const Sd::Config cfg = Sd::loadConfig();
    auto* root = new QVBoxLayout(this);

    m_libStatus = new QLabel;
    m_libStatus->setWordWrap(true);
    QString libErr;
    auto* intro = new QLabel(
        "Stable Diffusion (via <a href=\"https://github.com/leejet/stable-diffusion.cpp\">stable-diffusion.cpp</a>) a besoin de fichiers de "
        "modèle (plusieurs Go) qui ne sont pas fournis. Deux façons de les renseigner, sous deux onglets ci-dessous. Pour l'inpainting, "
        "tout modèle convient ; un modèle « inpainting » dédié donne souvent de meilleurs raccords. Tout le calcul est local (CPU).");
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setOpenExternalLinks(true);
    root->addWidget(intro);
    if (Sd::libraryLoaded(&libErr)) setStatusStyle(m_libStatus, "Bibliothèque : chargée (" + Sd::libraryPath() + ")", false);
    else setStatusStyle(m_libStatus, "⚠ Bibliothèque indisponible : " + libErr, true);
    root->addWidget(m_libStatus);

    auto* tabs = new QTabWidget;
    root->addWidget(tabs, 1);

    // ---- onglet 1 : checkpoint complet (cas courant) --------------------------------------------------------------
    auto* tab1 = new QWidget;
    auto* v1 = new QVBoxLayout(tab1);
    auto* intro1 = new QLabel(
        "<b>Cas le plus courant.</b> Un seul fichier contient tout : le modèle de diffusion (UNet), le VAE et l'encodeur de texte. "
        "C'est le format de la plupart des modèles SD 1.x, SD 2.x et SDXL distribués en un seul <code>.safetensors</code>, "
        "<code>.gguf</code> ou <code>.ckpt</code>. Si vous avez un seul fichier de plusieurs Go, c'est probablement celui-ci : "
        "remplissez uniquement le champ ci-dessous, laissez l'autre onglet vide.");
    intro1->setWordWrap(true);
    intro1->setTextFormat(Qt::RichText);
    v1->addWidget(intro1);
    auto* grid1 = new QGridLayout;
    grid1->setColumnStretch(1, 1);
    makeRow(grid1, 0, "model", "Checkpoint complet", cfg.model,
            "Le fichier principal du modèle (SD 1.x / 2.x / SDXL… en un seul fichier).");
    makeRow(grid1, 2, "vae", "VAE personnalisé (optionnel)", cfg.vae,
            "Remplace le VAE intégré au checkpoint, si besoin (ex. un VAE corrigé pour SD 1.5). Laissez vide pour utiliser celui du checkpoint.");
    v1->addLayout(grid1);
    v1->addStretch();
    tabs->addTab(tab1, "Checkpoint complet");

    // ---- onglet 2 : modèle de diffusion + encodeurs séparés (avancé) ----------------------------------------------
    auto* tab2 = new QWidget;
    auto* v2 = new QVBoxLayout(tab2);
    auto* intro2 = new QLabel(
        "<b>Cas avancé.</b> Pour les modèles distribués sans encodeur de texte intégré : <b>Flux</b> (CLIP-L + T5-XXL), "
        "<b>SD3</b> (CLIP-L + CLIP-G + T5-XXL), ou certains SDXL réencodés. Remplissez le modèle de diffusion ET les "
        "encodeurs que son architecture nécessite (pas forcément les trois). N'utilisez cet onglet <b>que si</b> l'onglet "
        "« Checkpoint complet » ne convient pas pour votre modèle — le bouton de diagnostic (icône ⓘ) à côté de chaque "
        "champ vous dira ce qu'il détecte dans un fichier donné.");
    intro2->setWordWrap(true);
    intro2->setTextFormat(Qt::RichText);
    v2->addWidget(intro2);
    auto* grid2 = new QGridLayout;
    grid2->setColumnStretch(1, 1);
    makeRow(grid2, 0, "diffusionModel", "Modèle de diffusion seul", cfg.diffusionModel,
            "L'UNet (ou les blocs de diffusion Flux/SD3) sans encodeur de texte ni VAE.");
    makeRow(grid2, 2, "clipL", "Encodeur CLIP-L", cfg.clipL, "Requis par la plupart des architectures (SD 1.x/2.x/SDXL/Flux/SD3).");
    makeRow(grid2, 4, "clipG", "Encodeur CLIP-G", cfg.clipG, "Requis par SDXL et SD3 uniquement. Laissez vide pour Flux.");
    makeRow(grid2, 6, "t5xxl", "Encodeur T5-XXL", cfg.t5xxl, "Requis par Flux et SD3. Laissez vide pour SD 1.x/2.x/SDXL classique.");
    v2->addLayout(grid2);
    v2->addStretch();
    tabs->addTab(tab2, "Modèle de diffusion + encodeurs");

    // ---- onglet 3 : performances -----------------------------------------------------------------------------------
    auto* tab3 = new QWidget;
    auto* v3 = new QVBoxLayout(tab3);
    auto* af = new QFormLayout;
    m_threads = new QSpinBox;
    m_threads->setRange(0, 128);
    m_threads->setSpecialValueText("Automatique (cœurs physiques)");
    m_threads->setValue(cfg.threads);
    m_flash = new QCheckBox("Attention flash (moins de mémoire, plus rapide sur certains processeurs)");
    m_flash->setChecked(cfg.flashAttention);
    m_mmap = new QCheckBox("Projection mémoire des poids (chargement plus rapide, moins de RAM)");
    m_mmap->setChecked(cfg.mmap);
    m_unload = new QCheckBox("Libérer la mémoire du modèle après chaque génération (plus lent, économise plusieurs Go)");
    m_unload->setChecked(cfg.unloadAfterUse);
    af->addRow("Fils de calcul :", m_threads);
    af->addRow(m_flash);
    af->addRow(m_mmap);
    af->addRow(m_unload);
    v3->addLayout(af);
    auto* free = button("Libérer la mémoire maintenant");
    connect(free, &QPushButton::clicked, this, [] { Sd::unloadModel(); });
    v3->addWidget(free, 0, Qt::AlignLeft);
    v3->addStretch();
    tabs->addTab(tab3, "Performances");

    m_globalStatus = new QLabel;
    m_globalStatus->setObjectName("SdGlobalStatus");
    m_globalStatus->setWordWrap(true);
    m_globalStatus->setMinimumHeight(20);
    root->addWidget(m_globalStatus);
    refreshGlobalStatus();

    auto* row = new QHBoxLayout;
    auto* ok = button("OK");
    auto* cancel = button("Annuler");
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    row->addStretch();
    row->addWidget(ok);
    row->addWidget(cancel);
    root->addLayout(row);
    setMinimumWidth(900);
    setMinimumHeight(560);
}

void SdSettingsDialog::refreshRow(Row& row) {
    const QString p = row.edit->text().trimmed();
    if (p.isEmpty()) { setStatusStyle(row.status, "", false); return; }
    if (QString bad = Sd::validateModelFile(p); !bad.isEmpty()) setStatusStyle(row.status, "✗ " + bad, true);
    else setStatusStyle(row.status, QString("✓ Fichier plausible (%1). Cliquez sur ⓘ pour un diagnostic détaillé.").arg(mu::humanSize(QFileInfo(p).size())), false);
}

void SdSettingsDialog::refreshGlobalStatus() {
    auto val = [&](const char* key) -> QString {
        for (const Row& r : m_rows) if (r.key == key) return r.edit->text().trimmed();
        return {};
    };
    const bool hasCkpt = !val("model").isEmpty();
    const bool hasDiff = !val("diffusionModel").isEmpty();
    if (!hasCkpt && !hasDiff) { setStatusStyle(m_globalStatus, "⚠ Aucun modèle configuré : remplissez au moins « Checkpoint complet » (onglet 1).", true); return; }
    if (hasCkpt && !hasDiff) { setStatusStyle(m_globalStatus, "✓ Configuration prête : checkpoint complet (onglet 1).", false); return; }
    if (hasDiff && !hasCkpt) {
        const bool clipL = !val("clipL").isEmpty(), clipG = !val("clipG").isEmpty(), t5 = !val("t5xxl").isEmpty();
        if (!clipL && !clipG && !t5) { setStatusStyle(m_globalStatus, "⚠ Modèle de diffusion renseigné mais aucun encodeur de texte : la génération échouera. Complétez l'onglet 2.", true); return; }
        setStatusStyle(m_globalStatus, "✓ Configuration prête : modèle de diffusion + encodeur(s) (onglet 2).", false);
        return;
    }
    setStatusStyle(m_globalStatus, "✓ Configuration prête : checkpoint complet ET modèle de diffusion renseignés — usage avancé, "
                                    "le modèle de diffusion remplacera l'UNet du checkpoint. Si ce n'est pas voulu, videz l'un des deux.", false);
}

void SdSettingsDialog::accept() {
    Sd::Config c;
    for (const Row& r : m_rows) {
        const QString v = r.edit->text().trimmed();
        if (r.key == "model") c.model = v; else if (r.key == "vae") c.vae = v; else if (r.key == "diffusionModel") c.diffusionModel = v;
        else if (r.key == "clipL") c.clipL = v; else if (r.key == "clipG") c.clipG = v; else if (r.key == "t5xxl") c.t5xxl = v;
    }
    c.threads = m_threads->value();
    c.flashAttention = m_flash->isChecked();
    c.mmap = m_mmap->isChecked();
    c.unloadAfterUse = m_unload->isChecked();
    Sd::saveConfig(c);      // le moteur recharge tout seul le modèle au prochain usage si la configuration a changé
    QDialog::accept();
}

// ============================================================================================ base commune
SdDialogBase::SdDialogBase(const QString& title, QWidget* parent) : MovableDialog(parent) {
    setWindowTitle(title);
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(8, 8, 8, 8);

    // ---- colonne de gauche : tout ce qui est verrouillé pendant un calcul
    auto* inputs = new QWidget;
    inputs->setFixedWidth(480);
    auto* iv = new QVBoxLayout(inputs);
    iv->setContentsMargins(0, 0, 0, 0);
    iv->setSpacing(4);
    iv->addWidget(new QLabel("Prompt (ce que l'on veut voir) :"));
    m_prompt = new QPlainTextEdit;
    m_prompt->setFixedHeight(56);
    m_prompt->setPlaceholderText("ex. un chat roux endormi sur un rebord de fenêtre, lumière douce, très détaillé");
    iv->addWidget(m_prompt);
    iv->addWidget(new QLabel("Prompt négatif (ce que l'on veut éviter) :"));
    m_negative = new QPlainTextEdit;
    m_negative->setFixedHeight(40);
    m_negative->setPlaceholderText("ex. flou, basse qualité, déformé, texte, filigrane");
    iv->addWidget(m_negative);

    auto* spec = new QGroupBox("Image");
    m_specific = new QFormLayout(spec);
    m_specific->setContentsMargins(8, 6, 8, 6);
    m_specific->setVerticalSpacing(4);
    iv->addWidget(spec);

    auto* gen = new QGroupBox("Génération");
    auto* gg = new QGridLayout(gen);
    gg->setContentsMargins(8, 6, 8, 6);
    gg->setVerticalSpacing(4);
    m_steps = new QSpinBox; m_steps->setRange(1, 150); m_steps->setValue(20);
    m_steps->setToolTip("Plus d'étapes = plus de détail mais plus lent (20–30 suffisent en général ; 4–8 pour les modèles « turbo/LCM »).");
    m_cfg = new QDoubleSpinBox; m_cfg->setRange(1.0, 30.0); m_cfg->setSingleStep(0.5); m_cfg->setValue(7.0);
    m_cfg->setToolTip("Fidélité au prompt (CFG). ~7 pour SD 1.x/XL ; 1–2 pour les modèles « turbo/LCM ».");
    m_batch = new QSpinBox; m_batch->setRange(1, 8); m_batch->setValue(1);
    m_batch->setToolTip("Nombre de variantes générées d'un coup (graines consécutives). Multiplie le temps de calcul.");
    m_randomSeed = new QCheckBox("Aléatoire");
    m_randomSeed->setChecked(true);
    m_seed = new QSpinBox; m_seed->setRange(0, 2147483647); m_seed->setEnabled(false);
    gg->addWidget(new QLabel("Étapes :"), 0, 0);  gg->addWidget(m_steps, 0, 1);
    gg->addWidget(new QLabel("CFG :"), 0, 2);     gg->addWidget(m_cfg, 0, 3);
    gg->addWidget(new QLabel("Variantes :"), 1, 0); gg->addWidget(m_batch, 1, 1);
    gg->addWidget(new QLabel("Graine :"), 1, 2);
    auto* seedRow = new QHBoxLayout;
    seedRow->addWidget(m_randomSeed);
    seedRow->addWidget(m_seed, 1);
    gg->addLayout(seedRow, 1, 3);
    gg->setColumnStretch(1, 1);
    gg->setColumnStretch(3, 2);

    auto* advToggle = new QToolButton;
    advToggle->setText("Options avancées ▸");
    advToggle->setCheckable(true);
    advToggle->setAutoRaise(true);
    auto* adv = new QWidget;
    auto* af = new QFormLayout(adv);
    af->setContentsMargins(0, 0, 0, 0);
    af->setVerticalSpacing(4);
    m_sampler = new QComboBox;
    m_sampler->addItem("Automatique (défaut du modèle)", "");
    for (const QString& n : Sd::samplerNames()) m_sampler->addItem(n, n);
    m_scheduler = new QComboBox;
    m_scheduler->addItem("Automatique (défaut du modèle)", "");
    for (const QString& n : Sd::schedulerNames()) m_scheduler->addItem(n, n);
    m_tiling = new QCheckBox("Décodage VAE par tuiles (économise la mémoire)");
    af->addRow("Échantillonneur :", m_sampler);
    af->addRow("Ordonnanceur :", m_scheduler);
    af->addRow(m_tiling);
    adv->hide();
    gg->addWidget(advToggle, 2, 0, 1, 4, Qt::AlignLeft);
    gg->addWidget(adv, 3, 0, 1, 4);
    connect(advToggle, &QToolButton::toggled, this, [advToggle, adv](bool on) { adv->setVisible(on); advToggle->setText(on ? "Options avancées ▾" : "Options avancées ▸"); });
    iv->addWidget(gen);
    iv->addStretch(1);
    connect(m_randomSeed, &QCheckBox::toggled, this, [this](bool on) { m_seed->setEnabled(!on); });

    // ---- colonne de droite : aperçu (ajouté par le dialogue dérivé), variantes, journal
    auto* right = new QWidget;
    right->setMinimumWidth(360);
    m_rightCol = new QVBoxLayout(right);
    m_rightCol->setContentsMargins(0, 0, 0, 0);
    m_rightCol->setSpacing(4);
    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(72, 72));
    m_list->setFixedHeight(104);
    m_list->setMovement(QListWidget::Static);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setWrapping(false);
    m_list->setToolTip("Variantes générées : cliquez pour choisir celle à conserver.");
    m_rightCol->addWidget(new QLabel("Résultats :"));
    m_rightCol->addWidget(m_list);
    m_logToggle = new QToolButton;
    m_logToggle->setText("Journal ▸");
    m_logToggle->setCheckable(true);
    m_logToggle->setAutoRaise(true);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFixedHeight(96);
    m_log->hide();
    m_rightCol->addWidget(m_logToggle, 0, Qt::AlignLeft);
    m_rightCol->addWidget(m_log);
    m_rightCol->addStretch(1);
    connect(m_logToggle, &QToolButton::toggled, this, [this](bool on) { m_log->setVisible(on); m_logToggle->setText(on ? "Journal ▾" : "Journal ▸"); });

    // ---- zone défilante : plafonnée à la hauteur de la fenêtre, pour que la barre du bas reste toujours accessible
    auto* body = new QWidget;
    auto* bh = new QHBoxLayout(body);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->setSpacing(12);
    bh->addWidget(inputs, 0, Qt::AlignTop);
    bh->addWidget(right, 1);
    m_body = body;
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidget(body);
    m_root->addWidget(m_scroll, 1);

    // ---- barre d'état et de commandes FIXE (hors de la zone défilante)
    m_stage = new QLabel;
    m_stage->setStyleSheet("color:#b0b0b0;");
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(34);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_seedInfo = new QLabel;
    m_seedInfo->setStyleSheet("color:#b0b0b0;");
    m_root->addWidget(m_stage);
    m_root->addWidget(m_statusLabel);
    m_root->addWidget(m_seedInfo);

    auto* bottom = new QHBoxLayout;
    m_generate = button("Générer");
    m_generate->setStyleSheet("font-weight:bold;");
    m_cancel = button("Annuler le calcul");
    m_cancel->setEnabled(false);
    m_bar = new QProgressBar;
    m_bar->setRange(0, 1);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setMinimumWidth(120);
    m_ok = button("Appliquer");
    m_ok->setEnabled(false);
    m_cancelDlg = button("Fermer");
    bottom->addWidget(m_generate);
    bottom->addWidget(m_cancel);
    bottom->addWidget(m_bar, 1);
    bottom->addSpacing(12);
    bottom->addWidget(m_ok);
    bottom->addWidget(m_cancelDlg);
    m_root->addLayout(bottom);

    connect(m_generate, &QPushButton::clicked, this, [this] { generate(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { cancelGeneration(); });
    connect(m_ok, &QPushButton::clicked, this, [this] { accept(); });
    connect(m_cancelDlg, &QPushButton::clicked, this, [this] { reject(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) { if (row >= 0) currentChanged(row); });
}

void SdDialogBase::finishLayout(const QString& okText) {
    m_ok->setText(okText);
    loadUi();
    // Les libellés du formulaire « Image » ne doivent jamais être compressés en dessous de leur taille naturelle
    // (sinon Qt rogne le texte quand la ligne est étroite, ex. « Placement : » -> « Placen »).
    for (int i = 0; i < m_specific->rowCount(); ++i)
        if (QLayoutItem* it = m_specific->itemAt(i, QFormLayout::LabelRole))
            if (auto* lbl = qobject_cast<QLabel*>(it->widget()))
                lbl->setMinimumWidth(lbl->sizeHint().width());
    // Taille de la zone défilante : tout le contenu si la fenêtre le permet, sinon plafonné (le reste défile).
    QWidget* mw = Workspace::instance().mainWindow();
    const int maxH = mw ? std::max(320, mw->height() - 230) : 620;
    const QSize hint = m_body->sizeHint();
    const bool scrolls = hint.height() > maxH;
    m_scroll->setMinimumSize(hint.width() + (scrolls ? 20 : 4), std::min(hint.height() + 4, maxH));
}

void SdDialogBase::loadUi() {
    QSettings s = uiSettings();
    m_prompt->setPlainText(s.value(key("prompt")).toString());
    m_negative->setPlainText(s.value(key("negative")).toString());
    m_steps->setValue(s.value(key("steps"), 20).toInt());
    m_cfg->setValue(s.value(key("cfg"), 7.0).toDouble());
    m_batch->setValue(s.value(key("batch"), 1).toInt());
    m_tiling->setChecked(s.value(key("tiling"), false).toBool());
    m_randomSeed->setChecked(s.value(key("randomSeed"), true).toBool());
    if (int i = m_sampler->findData(s.value(key("sampler")).toString()); i >= 0) m_sampler->setCurrentIndex(i);
    if (int i = m_scheduler->findData(s.value(key("scheduler")).toString()); i >= 0) m_scheduler->setCurrentIndex(i);
}

void SdDialogBase::saveUi() {
    QSettings s = uiSettings();
    s.setValue(key("prompt"), m_prompt->toPlainText());
    s.setValue(key("negative"), m_negative->toPlainText());
    s.setValue(key("steps"), m_steps->value());
    s.setValue(key("cfg"), m_cfg->value());
    s.setValue(key("batch"), m_batch->value());
    s.setValue(key("tiling"), m_tiling->isChecked());
    s.setValue(key("randomSeed"), m_randomSeed->isChecked());
    s.setValue(key("sampler"), m_sampler->currentData().toString());
    s.setValue(key("scheduler"), m_scheduler->currentData().toString());
}

SdDialogBase::~SdDialogBase() {
    if (m_job && m_job->isRunning()) {     // destruction pendant un calcul : ne jamais laisser le fil écrire dans un objet détruit
        m_job->disconnect(this);
        if (m_active) m_job->cancel();
        m_job->wait();
    }
}

// « En cours » = résultat pas encore reçu. On ne se fie PAS à QThread::isRunning() : le signal de résultat part avant la toute fin du fil.
bool SdDialogBase::running() const { return m_active; }
int SdDialogBase::currentResult() const { return m_list->currentRow(); }
QString SdDialogBase::promptText() const { return m_prompt->toPlainText().trimmed(); }
QString SdDialogBase::statusText() const { return m_statusLabel->text(); }

cv::Mat SdDialogBase::selectedImage() const {
    const int i = m_list->currentRow();
    return (i >= 0 && i < int(m_results.size())) ? m_results[size_t(i)] : cv::Mat();
}

void SdDialogBase::setStatus(const QString& text, bool error) { setStatusStyle(m_statusLabel, text, error); }

void SdDialogBase::setRunning(bool on) {
    if (auto* inputs = m_prompt->parentWidget()) inputs->setEnabled(!on);
    m_generate->setEnabled(!on);
    m_cancel->setEnabled(on);
    m_ok->setEnabled(!on && !m_results.empty());
    if (on) m_bar->setRange(0, 0);            // barre « occupé » tant que la première étape n'a pas commencé
    else { m_bar->setRange(0, 1); m_bar->setValue(0); m_stage->clear(); }
}

bool SdDialogBase::generate() {
    if (running()) return false;
    Sd::Request r;
    r.mode = mode();
    r.prompt = m_prompt->toPlainText().trimmed();
    r.negative = m_negative->toPlainText().trimmed();
    r.steps = m_steps->value();
    r.cfg = float(m_cfg->value());
    r.seed = m_randomSeed->isChecked() ? -1 : m_seed->value();
    r.sampler = m_sampler->currentData().toString();
    r.scheduler = m_scheduler->currentData().toString();
    r.batch = m_batch->value();
    r.vaeTiling = m_tiling->isChecked();
    r.config = Sd::loadConfig();
    QString err;
    if (!buildRequest(r, &err)) { setStatus(err, true); return false; }
    Sd::Job* job = Sd::start(r, &err);
    if (!job) { setStatus(err, true); return false; }

    saveUi();
    m_job = job;
    m_active = true;
    m_log->clear();
    m_seedInfo->clear();
    setStatus("", false);
    setRunning(true);
    m_stage->setText("Démarrage…");
    connect(job, &Sd::Job::stageChanged, this, [this](const QString& s) { m_stage->setText(s); });
    connect(job, &Sd::Job::progress, this, [this](int step, int steps, double sps) {
        m_bar->setRange(0, std::max(1, steps));
        m_bar->setValue(std::clamp(step, 0, steps));
        QString t = QString("Étape %1 / %2").arg(step).arg(steps);
        if (sps > 0.0) t += QString(" — %1 s/étape — reste ≈ %2 s").arg(sps, 0, 'f', 1).arg(int(std::lround((steps - step) * sps)));
        m_stage->setText(t);
    });
    connect(job, &Sd::Job::logLine, this, [this](int level, const QString& s) {
        static const char* tags[] = {"", "", "", "⚠ ", "✗ "};
        m_log->appendPlainText(QString(tags[std::clamp(level, 0, 4)]) + s);
    });
    connect(job, &Sd::Job::resultReady, this, [this](const Sd::Result& res) { onResult(res); });
    return true;
}

void SdDialogBase::cancelGeneration() {
    if (!running()) return;
    m_cancel->setEnabled(false);
    m_stage->setText("Annulation en cours… (effective à la fin de l'étape de calcul en cours ; le chargement du modèle ne peut pas être interrompu)");
    if (m_job) m_job->cancel();
}

void SdDialogBase::onResult(const Sd::Result& res) {
    m_active = false;
    setRunning(false);
    if (res.cancelled) {
        setStatus("Génération annulée.", false);
    } else if (!res.ok) {
        QString msg = res.error.isEmpty() ? QString("Échec de la génération.") : res.error;
        setStatus(msg, true);
        if (!res.logTail.isEmpty()) { m_logToggle->setChecked(true); }
    } else {
        m_results = res.images;
        m_lastSeed = res.seed;
        m_list->clear();
        for (size_t i = 0; i < m_results.size(); ++i) {
            auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(mu::thumbnail(m_results[i], QSize(88, 88), false))), QString("#%1").arg(i + 1));
            m_list->addItem(it);
        }
        m_list->setCurrentRow(0);
        m_seed->setValue(int(std::min<qint64>(res.seed, 2147483647)));
        m_seedInfo->setText(QString("Graine utilisée : %1 (décochez « Aléatoire » pour la réutiliser)").arg(res.seed));
        setStatus(QString("%1 image(s) générée(s) en %2 s%3.").arg(m_results.size()).arg(res.seconds, 0, 'f', 1)
                      .arg(res.modelVersion.isEmpty() ? QString() : " — modèle : " + res.modelVersion), false);
        m_ok->setEnabled(true);
        resultsReady();
    }
    emit generationFinished(res.ok, res.cancelled);
    if (m_closing) { m_closing = false; reject(); }
}

void SdDialogBase::accept() {
    if (running()) { setStatus("Un calcul est en cours : attendez sa fin ou annulez-le.", true); return; }
    if (selectedImage().empty()) { setStatus("Générez d'abord une image.", true); return; }
    saveUi();
    aboutToClose();
    MovableDialog::accept();
}

void SdDialogBase::reject() {
    if (running()) {                        // fermeture demandée pendant un calcul : annuler, puis se fermer à la fin réelle du fil
        m_closing = true;
        m_generate->setEnabled(false);
        m_ok->setEnabled(false);
        m_cancelDlg->setEnabled(false);
        cancelGeneration();
        return;
    }
    saveUi();
    aboutToClose();
    MovableDialog::reject();
}

// ============================================================================================ génération texte -> image
SdGenerateDialog::SdGenerateDialog(Document* doc, QWidget* parent) : SdDialogBase("Générer une image (Stable Diffusion)", parent), m_doc(doc) {
    QSettings s = uiSettings();
    m_w = new QSpinBox; m_w->setRange(64, 2048); m_w->setSingleStep(8); m_w->setValue(s.value(key("w"), 512).toInt());
    m_h = new QSpinBox; m_h->setRange(64, 2048); m_h->setSingleStep(8); m_h->setValue(s.value(key("h"), 512).toInt());
    m_w->setSuffix(" px"); m_h->setSuffix(" px");
    m_w->setToolTip("Multiple de 8. Restez proche de la taille d'entraînement du modèle (512 pour SD 1.x, 1024 pour SDXL) : au-delà, les images se répètent ou se déforment.");
    auto* sizeRow = new QVBoxLayout;
    auto* size = new QHBoxLayout;
    size->addWidget(m_w);
    size->addWidget(new QLabel("×"));
    size->addWidget(m_h);
    size->addStretch();
    sizeRow->addLayout(size);
    if (doc) {
        auto* fit = button("Proportions du document");
        connect(fit, &QPushButton::clicked, this, [this] {
            const double s = 1024.0 / std::max(m_doc->size().width(), m_doc->size().height());
            const double k = std::min(1.0, s);
            m_w->setValue(Sd::roundTo8(m_doc->size().width() * k));
            m_h->setValue(Sd::roundTo8(m_doc->size().height() * k));
        });
        sizeRow->addWidget(fit, 0, Qt::AlignLeft);
    }
    specificForm()->addRow("Taille :", sizeRow);

    m_placement = new QComboBox;
    m_placement->addItems({"Centrée, taille d'origine", "Ajustée au document", "Remplir le document (rognée)", "Dans la sélection"});
    m_placement->setToolTip("Centrée : taille d'origine. Ajustée : tient en entier (proportions conservées). Remplir : couvre tout le document (rognée). Dans la sélection : découpée selon sa forme.");
    m_placement->setCurrentIndex(std::clamp(s.value(key("placement"), 1).toInt(), 0, 3));
    if (doc) specificForm()->addRow("Placement :", m_placement);
    else specificForm()->addRow(new QLabel("Aucun document ouvert : l'image générée créera un nouveau document."));

    m_preview = new QLabel;
    m_preview->setFixedSize(320, 220);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("background:#1e1e1e;border:1px solid #555;");
    rightColumn()->insertWidget(0, m_preview);
    finishLayout(doc ? "Ajouter au document" : "Créer un document");
}

Sd::Placement SdGenerateDialog::placement() const { return Sd::Placement(m_placement->currentIndex()); }

bool SdGenerateDialog::buildRequest(Sd::Request& r, QString*) {
    r.width = m_w->value();
    r.height = m_h->value();
    return true;
}

void SdGenerateDialog::updatePreview() {
    const cv::Mat img = selectedImage();
    if (img.empty()) { m_preview->clear(); return; }
    m_preview->setPixmap(QPixmap::fromImage(mu::thumbnail(img, m_preview->size() - QSize(4, 4), false)));
}

void SdGenerateDialog::aboutToClose() {
    QSettings s = uiSettings();
    s.setValue(key("w"), m_w->value());
    s.setValue(key("h"), m_h->value());
    s.setValue(key("placement"), m_placement->currentIndex());
}

// ============================================================================================ inpainting sur la sélection
SdInpaintDialog::SdInpaintDialog(Document* doc, QWidget* parent)
    : SdDialogBase("Inpainting sur la sélection (Stable Diffusion)", parent), m_doc(doc), m_layer(doc->activeLayer()) {
    m_orig = m_layer->image;       // en-tête partagé : l'aperçu remplace `image`, jamais de modification sur place
    QSettings s = uiSettings();

    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setStyleSheet("color:#b0b0b0;");
    specificForm()->addRow(m_info);

    m_res = new QComboBox;
    for (int v : {384, 512, 640, 768, 1024, 1536}) m_res->addItem(QString("%1 px").arg(v), v);
    m_res->setCurrentIndex(std::max(0, m_res->findData(s.value(key("res"), 512))));
    m_res->setToolTip("Taille de l'image envoyée au modèle : 512 pour SD 1.x, 1024 pour SDXL. Plus grand = plus lent et plus de mémoire.");
    m_context = new QSpinBox; m_context->setRange(0, 300); m_context->setSuffix(" %"); m_context->setValue(s.value(key("ctx"), 50).toInt());
    m_context->setToolTip("Marge d'image autour de la zone, donnée au modèle pour qu'il raccorde le contenu à son environnement.");
    m_expand = new QSpinBox; m_expand->setRange(0, 64); m_expand->setSuffix(" px"); m_expand->setValue(s.value(key("expand"), 4).toInt());
    m_expand->setToolTip("Agrandit la zone à régénérer pour ne laisser aucun liseré de l'ancien contenu.");
    m_feather = new QSpinBox; m_feather->setRange(0, 64); m_feather->setSuffix(" px"); m_feather->setValue(s.value(key("feather"), 6).toInt());
    m_feather->setToolTip("Adoucit le raccord entre le contenu régénéré et l'image d'origine.");
    m_strength = new SliderSpin(0.05, 1.0, s.value(key("strength"), 0.8).toDouble(), 2);
    m_strength->setToolTip("1.0 = remplacer complètement le contenu de la zone ; plus bas = rester proche de l'existant (retouche légère).");
    m_merged = new QCheckBox("Fusionner tous les calques visibles");
    m_merged->setChecked(s.value(key("merged"), true).toBool());
    m_livePreview = new QCheckBox("Aperçu en direct sur le calque");
    m_livePreview->setChecked(true);
    specificForm()->addRow("Résolution de travail :", m_res);
    specificForm()->addRow("Contexte autour de la zone :", m_context);
    specificForm()->addRow("Étendre la zone de :", m_expand);
    specificForm()->addRow("Raccord adouci de :", m_feather);
    specificForm()->addRow("Force de régénération :", m_strength);
    specificForm()->addRow(m_merged);
    specificForm()->addRow(m_livePreview);
    finishLayout("Appliquer à la sélection");

    m_infoTimer = new QTimer(this);
    m_infoTimer->setSingleShot(true);
    m_infoTimer->setInterval(150);
    connect(m_infoTimer, &QTimer::timeout, this, [this] { refreshInfo(); });
    for (QSpinBox* sp : {m_context, m_expand, m_feather}) connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { m_infoTimer->start(); });
    connect(m_res, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { m_infoTimer->start(); });
    connect(m_livePreview, &QCheckBox::toggled, this, [this] { showPreview(); });
    QTimer::singleShot(0, this, [this] { refreshInfo(); });
}

Sd::InpaintPlanParams SdInpaintDialog::planParams() const {
    Sd::InpaintPlanParams p;
    p.workRes = m_res->currentData().toInt();
    p.contextPercent = m_context->value();
    p.expand = m_expand->value();
    p.feather = m_feather->value();
    return p;
}

void SdInpaintDialog::refreshInfo() {
    Sd::InpaintPlan plan;
    QString err;
    if (!Sd::planInpaint(m_orig, m_doc->selection(), planParams(), &plan, &err)) { m_info->setText("⚠ " + err); return; }
    QString t = QString("Zone à régénérer : %1 × %2 px → recadrage %3 × %4 px → image de travail %5 × %6 px.")
                    .arg(plan.region.width).arg(plan.region.height).arg(plan.crop.width).arg(plan.crop.height).arg(plan.work.width).arg(plan.work.height);
    if (std::max(plan.crop.width, plan.crop.height) > 2 * std::max(plan.work.width, plan.work.height))
        t += "\n⚠ La zone est bien plus grande que la résolution de travail : le détail sera réduit. Augmentez la résolution ou sélectionnez une zone plus petite.";
    t += "\nLes réglages de zone s'appliquent à la prochaine génération.";
    m_info->setText(t);
}

bool SdInpaintDialog::buildRequest(Sd::Request& r, QString* error) {
    restorePreview();                       // le contexte doit être l'image d'origine, pas un aperçu précédent
    if (!m_doc->hasSelection()) { if (error) *error = "Aucune sélection."; return false; }
    const cv::Mat source = m_merged->isChecked() ? m_doc->compositeCopy() : m_orig;
    Sd::InpaintPlan plan;
    if (!Sd::planInpaint(source, m_doc->selection(), planParams(), &plan, error)) return false;
    m_plan = plan;
    r.width = plan.work.width;
    r.height = plan.work.height;
    r.initImage = plan.workImage;
    r.mask = plan.workMask;
    r.strength = float(m_strength->value());
    QSettings s = uiSettings();
    s.setValue(key("res"), m_res->currentData());
    s.setValue(key("ctx"), m_context->value());
    s.setValue(key("expand"), m_expand->value());
    s.setValue(key("feather"), m_feather->value());
    s.setValue(key("strength"), m_strength->value());
    s.setValue(key("merged"), m_merged->isChecked());
    return true;
}

void SdInpaintDialog::showPreview() {
    if (!m_livePreview->isChecked()) { restorePreview(); return; }
    const cv::Mat gen = selectedImage();
    if (gen.empty() || gen.size() != m_plan.work) return;
    m_layer->image = Sd::compositeInpaint(m_orig, m_plan, gen);
    m_doc->invalidateAll();
}

void SdInpaintDialog::restorePreview() {
    if (m_layer->image.data != m_orig.data) {
        m_layer->image = m_orig;
        m_doc->invalidateAll();
    }
}
