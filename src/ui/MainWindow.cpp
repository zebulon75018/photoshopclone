#include "MainWindow.h"
#include "CanvasView.h"
#include "Dialogs.h"
#include "EffectDialog.h"
#include "OptionsBar.h"
#include "Panels.h"
#include "ToolBox.h"
#include "core/ImageIO.h"
#include "core/MatUtil.h"
#include "core/Operations.h"
#include "core/Workspace.h"
#include "tools/ToolManager.h"
#include "tools/Tools.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include "AIDialogs.h"
#include "SdDialogs.h"
#include "ai/SdBackend.h"
#include "MovableDialog.h"
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QUndoStack>
#include <QMenuBar>

static Workspace& WS() { return Workspace::instance(); }

MainWindow::MainWindow() {
    setWindowTitle("PhotoClone");
    WS().setMainWindow(this);
    setAcceptDrops(true);
    resize(1500, 900);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true); m_tabs->setMovable(true); m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

    m_options = new OptionsBar;
    addToolBar(Qt::TopToolBarArea, m_options);
    m_toolbox = new ToolBox(WS().tools());
    addToolBar(Qt::LeftToolBarArea, m_toolbox);

    buildDocks();
    buildStatusBar();
    buildMenus();
    buildToolShortcuts();

    auto* tm = WS().tools();
    auto rebuildOptions = [this, tm] {
        m_options->clearOptions();
        if (tm->current()) tm->current()->buildOptions(*m_options);
        if (view()) { view()->updateToolCursor(); view()->refresh(); }
    };
    connect(tm, &ToolManager::toolChanged, this, rebuildOptions);
    connect(&WS(), &Workspace::settingsChanged, this, [this] { if (view()) view()->refresh(); });
    connect(&WS(), &Workspace::messageRequested, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    rebuildOptions();

    QSettings s("PhotoClone", "PhotoClone");
    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("state").toByteArray());
}

MainWindow::~MainWindow() {
    Sd::shutdown();
    WS().setMainWindow(nullptr);
    m_closing = true;   // les Documents sont détruits avec les vues : ne plus toucher à l'interface
    WS().setView(nullptr);
    m_layers->setDocument(nullptr); m_history->setDocument(nullptr); m_histogram->setDocument(nullptr);
}

// ------------------------------------------------------------------------------------------ construction
QAction* MainWindow::add(QMenu* m, const QString& text, const QString& sc, const std::function<void()>& fn, bool flushTool) {
    QAction* a = m->addAction(text);
    if (!sc.isEmpty()) a->setShortcut(QKeySequence(sc));
    connect(a, &QAction::triggered, this, [fn, flushTool] {
        if (Workspace::instance().modalLocked()) return;
        // une transformation en cours (pixels ou sélection) est validée avant toute action qui modifie le document
        if (flushTool) if (Tool* t = Workspace::instance().tools()->current()) t->flush();
        fn();
    });
    return a;
}

std::function<void()> MainWindow::onDoc(const std::function<void(Document*)>& f) {
    return [this, f] { if (Document* d = doc()) f(d); };
}

Document* MainWindow::doc() const { return WS().doc(); }
CanvasView* MainWindow::view() const { return WS().view(); }

void MainWindow::buildDocks() {
    auto mk = [&](const QString& title, const QString& name, QWidget* w) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name); d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        m_docks << d;
        return d;
    };
    m_color = new ColorPanel; m_swatches = new SwatchesPanel; m_histogram = new HistogramPanel;
    m_layers = new LayersPanel; m_history = new HistoryPanel;
    auto* dColor = mk(tr("Couleur"), "dockColor", m_color);
    auto* dSw = mk(tr("Nuancier"), "dockSwatches", m_swatches);
    auto* dHist = mk(tr("Histogramme"), "dockHistogram", m_histogram);
    auto* dLay = mk(tr("Calques"), "dockLayers", m_layers);
    auto* dHis = mk(tr("Historique"), "dockHistory", m_history);
    for (auto* d : {dColor, dHist, dLay}) addDockWidget(Qt::RightDockWidgetArea, d);
    tabifyDockWidget(dColor, dSw);
    tabifyDockWidget(dLay, dHis);
    dColor->raise(); dLay->raise();
    resizeDocks({dColor, dHist, dLay}, {200, 170, 420}, Qt::Vertical);
    resizeDocks({dColor}, {290}, Qt::Horizontal);
}

void MainWindow::buildStatusBar() {
    m_sizeLabel = new QLabel; m_posLabel = new QLabel; m_zoomLabel = new QLabel("100 %");
    for (QLabel* l : {m_sizeLabel, m_posLabel, m_zoomLabel}) l->setMinimumWidth(110);
    m_posLabel->setMinimumWidth(240);
    statusBar()->addPermanentWidget(m_sizeLabel);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->showMessage(tr("Ctrl+N : nouveau document • Ctrl+O : ouvrir • Espace : main • Alt+molette : zoom"), 8000);
}

void MainWindow::buildToolShortcuts() {
    const auto& groups = WS().tools()->groups();
    for (int g = 0; g < groups.size(); ++g) {
        auto* s1 = new QShortcut(QKeySequence(groups[g].key), this);
        connect(s1, &QShortcut::activated, this, [g] { if (!WS().modalLocked()) WS().tools()->selectGroup(g); });
        auto* s2 = new QShortcut(QKeySequence("Shift+" + groups[g].key), this);
        connect(s2, &QShortcut::activated, this, [g] { if (!WS().modalLocked()) WS().tools()->cycleGroup(g); });
    }
    connect(new QShortcut(QKeySequence("X"), this), &QShortcut::activated, this, [] { if (!WS().modalLocked()) WS().swapColors(); });
    connect(new QShortcut(QKeySequence("D"), this), &QShortcut::activated, this, [] { if (!WS().modalLocked()) WS().resetColors(); });
}

void MainWindow::buildMenus() {
    // ---------------------------------------------------------------- Fichier
    QMenu* f = menuBar()->addMenu(tr("&Fichier"));
    add(f, tr("Nouveau…"), "Ctrl+N", [this] { newDocument(); });
    add(f, tr("Ouvrir…"), "Ctrl+O", [this] { openDialog(); });
    m_recentMenu = f->addMenu(tr("Ouvrir un fichier récent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        for (const QString& p : QSettings("PhotoClone", "PhotoClone").value("recent").toStringList())
            m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] { openPath(p); });
        if (m_recentMenu->isEmpty()) m_recentMenu->addAction(tr("(vide)"))->setEnabled(false);
    });
    f->addSeparator();
    add(f, tr("Fermer"), "Ctrl+W", [this] { if (m_tabs->count()) closeTab(m_tabs->currentIndex()); });
    add(f, tr("Tout fermer"), "Ctrl+Alt+W", [this] { while (m_tabs->count()) { int n = m_tabs->count(); closeTab(m_tabs->count() - 1); if (m_tabs->count() == n) break; } });
    f->addSeparator();
    add(f, tr("Enregistrer"), "Ctrl+S", onDoc([this](Document* d) { saveDoc(d, false); }));
    add(f, tr("Enregistrer sous…"), "Ctrl+Shift+S", onDoc([this](Document* d) { saveDoc(d, true); }));
    add(f, tr("Exporter sous (image aplatie)…"), "Ctrl+Alt+Shift+W", [this] { exportAs(); });
    f->addSeparator();
    add(f, tr("Quitter"), "Ctrl+Q", [this] { close(); });

    // ---------------------------------------------------------------- Édition
    QMenu* e = menuBar()->addMenu(tr("&Édition"));
    add(e, tr("Annuler"), "Ctrl+Z", onDoc([](Document* d) { d->undoStack()->undo(); }));
    add(e, tr("Rétablir"), "Ctrl+Shift+Z", onDoc([](Document* d) { d->undoStack()->redo(); }));
    add(e, tr("Pas en arrière"), "Ctrl+Alt+Z", onDoc([](Document* d) { d->undoStack()->undo(); }));
    e->addSeparator();
    add(e, tr("Couper"), "Ctrl+X", onDoc([](Document* d) { Ops::cut(d); }));
    add(e, tr("Copier"), "Ctrl+C", onDoc([](Document* d) { Ops::copy(d, false); }));
    add(e, tr("Copier avec fusion"), "Ctrl+Shift+C", onDoc([](Document* d) { Ops::copy(d, true); }));
    add(e, tr("Coller"), "Ctrl+V", [this] { doPaste(false); });
    add(e, tr("Coller sur place"), "Ctrl+Shift+V", [this] { doPaste(true); });
    add(e, tr("Effacer"), "Delete", onDoc([](Document* d) { Ops::clearSelection(d); }));
    e->addSeparator();
    add(e, tr("Remplir avec la couleur de premier plan"), "Alt+Backspace", onDoc([](Document* d) { Ops::fillSelection(d, WS().fg, tr("Remplir (premier plan)")); }));
    add(e, tr("Remplir avec la couleur d'arrière-plan"), "Ctrl+Backspace", onDoc([](Document* d) { Ops::fillSelection(d, WS().bg, tr("Remplir (arrière-plan)")); }));
    add(e, tr("Remplissage d'après le contenu (comblement)…"), "Shift+F5", [this] { runEffect("retouch.inpaint"); });
    e->addSeparator();
    add(e, tr("Transformation manuelle"), "Ctrl+T", [this] { startTransform(); });
    QMenu* lt = e->addMenu(tr("Transformation du calque"));
    add(lt, tr("Miroir horizontal"), "", onDoc([](Document* d) { Ops::flipLayer(d, true); }));
    add(lt, tr("Miroir vertical"), "", onDoc([](Document* d) { Ops::flipLayer(d, false); }));

    // ---------------------------------------------------------------- Image
    QMenu* im = menuBar()->addMenu(tr("&Image"));
    auto& reg = EffectRegistry::instance();
    QMenu* adj = im->addMenu(tr("Réglages"));
    static const QMap<QString, QString> adjKeys = {
        {"adjust.levels", "Ctrl+L"}, {"adjust.curves", "Ctrl+M"}, {"adjust.huesat", "Ctrl+U"}, {"adjust.colorbalance", "Ctrl+B"},
        {"adjust.bw", "Ctrl+Alt+Shift+B"}, {"adjust.invert", "Ctrl+I"}, {"adjust.desaturate", "Ctrl+Shift+U"}, {"adjust.autotone", "Ctrl+Shift+L"},
        {"adjust.autocontrast", "Ctrl+Alt+Shift+L"}, {"adjust.autocolor", "Ctrl+Shift+B"}};
    QMenu* autoMenu = nullptr;
    for (const EffectPtr& ef : reg.all()) {
        if (!ef->id.startsWith("adjust.")) continue;
        QMenu* target = adj;
        if (ef->id.startsWith("adjust.auto")) { if (!autoMenu) { adj->addSeparator(); autoMenu = adj; } }
        QString id = ef->id;
        add(target, ef->name, adjKeys.value(id), [this, id] { runEffect(id); });
    }
    im->addSeparator();
    add(im, tr("Taille de l'image…"), "Ctrl+Alt+I", onDoc([this](Document* d) { ImageSizeDialog dlg(this, d->size()); if (dlg.exec() == QDialog::Accepted) Ops::resizeImage(d, dlg.newSize(), dlg.interpolation()); }));
    add(im, tr("Taille de la zone de travail…"), "Ctrl+Alt+C", onDoc([this](Document* d) { CanvasSizeDialog dlg(this, d->size()); if (dlg.exec() == QDialog::Accepted) Ops::resizeCanvas(d, dlg.newSize(), dlg.anchor()); }));
    QMenu* rot = im->addMenu(tr("Rotation de la zone de travail"));
    add(rot, "180°", "", onDoc([](Document* d) { Ops::rotateImage(d, 180); }));
    add(rot, tr("90° horaire"), "", onDoc([](Document* d) { Ops::rotateImage(d, 90); }));
    add(rot, tr("90° anti-horaire"), "", onDoc([](Document* d) { Ops::rotateImage(d, 270); }));
    rot->addSeparator();
    add(rot, tr("Miroir horizontal"), "", onDoc([](Document* d) { Ops::flipImage(d, true); }));
    add(rot, tr("Miroir vertical"), "", onDoc([](Document* d) { Ops::flipImage(d, false); }));
    add(im, tr("Recadrer selon la sélection"), "", onDoc([](Document* d) {
        if (d->hasSelection()) Ops::cropTo(d, mu::toQt(Sel::bounds(d->selection())));
        else WS().message(tr("Aucune sélection : utilisez l'outil Recadrage (C)."));
    }));

    // ---------------------------------------------------------------- Calque
    QMenu* l = menuBar()->addMenu(tr("&Calque"));
    add(l, tr("Nouveau calque"), "Ctrl+Shift+N", onDoc([](Document* d) { Ops::addLayer(d); }));
    add(l, tr("Calque par copier / Dupliquer"), "Ctrl+J", onDoc([](Document* d) { Ops::layerViaCopy(d, false); }));
    add(l, tr("Calque par couper"), "Ctrl+Shift+J", onDoc([](Document* d) { Ops::layerViaCopy(d, true); }));
    add(l, tr("Supprimer le calque"), "", onDoc([](Document* d) { Ops::deleteLayer(d); }));
    l->addSeparator();
    QMenu* mk = l->addMenu(tr("Masque de fusion"));
    add(mk, tr("Tout faire apparaître / selon la sélection"), "", onDoc([](Document* d) { Ops::addMask(d, false); }));
    add(mk, tr("Tout masquer"), "", onDoc([](Document* d) { Ops::addMask(d, true); }));
    add(mk, tr("Appliquer le masque"), "", onDoc([](Document* d) { Ops::deleteMask(d, true); }));
    add(mk, tr("Supprimer le masque"), "", onDoc([](Document* d) { Ops::deleteMask(d, false); }));
    add(l, tr("Pixelliser le texte"), "", onDoc([](Document* d) { Ops::rasterizeText(d); }));
    l->addSeparator();
    QMenu* ar = l->addMenu(tr("Disposition"));
    add(ar, tr("Premier plan"), "Ctrl+Shift+]", onDoc([](Document* d) { Ops::moveLayerToEnd(d, true); }));
    add(ar, tr("Avancer"), "Ctrl+]", onDoc([](Document* d) { Ops::moveLayer(d, 1); }));
    add(ar, tr("Reculer"), "Ctrl+[", onDoc([](Document* d) { Ops::moveLayer(d, -1); }));
    add(ar, tr("Arrière-plan"), "Ctrl+Shift+[", onDoc([](Document* d) { Ops::moveLayerToEnd(d, false); }));
    l->addSeparator();
    add(l, tr("Fusionner vers le bas"), "Ctrl+E", onDoc([](Document* d) { Ops::mergeDown(d); }));
    add(l, tr("Fusionner les calques visibles"), "Ctrl+Shift+E", onDoc([](Document* d) { Ops::mergeVisible(d); }));
    add(l, tr("Aplatir l'image"), "", onDoc([](Document* d) { Ops::flatten(d); }));

    // ---------------------------------------------------------------- Sélection
    QMenu* s = menuBar()->addMenu(tr("&Sélection"));
    add(s, tr("Tout sélectionner"), "Ctrl+A", onDoc([](Document* d) { Ops::selectAll(d); }));
    add(s, tr("Désélectionner"), "Ctrl+D", onDoc([](Document* d) { Ops::deselect(d); }));
    add(s, tr("Resélectionner"), "Ctrl+Shift+D", onDoc([](Document* d) { d->reselect(); }));
    add(s, tr("Intervertir la sélection"), "Ctrl+Shift+I", onDoc([](Document* d) { Ops::invertSelection(d); }));
    add(s, tr("Transformer la sélection (agrandir / pivoter)"), "", [this] {
        auto* tm = WS().tools(); Tool* t = tm->byId("sel_transform");
        if (tm->current() == t) t->activate(); else tm->select(t);
    });
    s->addSeparator();
    QMenu* mod = s->addMenu(tr("Modifier"));
    add(mod, tr("Contour progressif…"), "Shift+F6", [this] { modifySelectionPrompt(0, tr("Contour progressif"), tr("Rayon (px) :"), 5, 250); });
    add(mod, tr("Agrandir…"), "", [this] { modifySelectionPrompt(1, tr("Agrandir la sélection"), tr("De (px) :"), 5, 500); });
    add(mod, tr("Contracter…"), "", [this] { modifySelectionPrompt(2, tr("Contracter la sélection"), tr("De (px) :"), 5, 500); });
    add(mod, tr("Lisser…"), "", [this] { modifySelectionPrompt(3, tr("Lisser la sélection"), tr("Rayon (px) :"), 5, 100); });

    // ---------------------------------------------------------------- Filtre
    QMenu* fl = menuBar()->addMenu(tr("Fi&ltre"));
    add(fl, tr("Dernier filtre"), "Ctrl+F", [this] { repeatLastFilter(); });
    add(fl, tr("Dernier filtre (avec options)…"), "Ctrl+Alt+F", [this] { if (m_lastEffect) runEffect(m_lastEffect->id, true); });
    fl->addSeparator();
    for (const QString& cat : reg.categories("filter")) {
        QMenu* sub = fl->addMenu(cat);
        for (const EffectPtr& ef : reg.all())
            if (ef->id.startsWith("filter.") && ef->category == cat) { QString id = ef->id; add(sub, ef->name, "", [this, id] { runEffect(id); }); }
    }

    // ---------------------------------------------------------------- IA (vision.cpp)
    QMenu* ai = menuBar()->addMenu(tr("&IA"));
    add(ai, tr("Réglages des modèles…"), "", [this] { AIModelsDialog dlg(this); dlg.exec(); }, false);
    ai->addSeparator();
    add(ai, tr("Supprimer l'arrière-plan…"), "Ctrl+Alt+K", [this] { cmdRemoveBackground(); });
    add(ai, tr("Sélection par IA (MobileSAM) : cliquer / cadrer un objet"), "", [this] {
        WS().tools()->select(WS().tools()->byId("sam"));
        if (!AIModels::isConfigured(AIArchitecture::Sam)) statusBar()->showMessage(tr("MobileSAM : aucun modèle configuré (menu IA > Réglages des modèles…)."), 6000);
    }, false);
    add(ai, tr("Remplissage IA (MI-GAN)…"), "", [this] { if (ensureAIModel(AIArchitecture::MiGan)) runEffect("retouch.inpaint", false, {{"algo", 2}}); });
    add(ai, tr("Carte de profondeur (Depth-Anything)…"), "", [this] { cmdDepth(); });
    add(ai, tr("Agrandissement IA (Real-ESRGAN)…"), "", [this] { cmdUpscale(); });
    ai->addSeparator();
    add(ai, tr("Générer une image (Stable Diffusion)…"), "Ctrl+Alt+G", [this] { cmdSdGenerate(); });
    add(ai, tr("Inpainting sur la sélection (Stable Diffusion)…"), "Ctrl+Alt+P", [this] { cmdSdInpaint(); });
    add(ai, tr("Réglages de Stable Diffusion…"), "", [this] { SdSettingsDialog dlg(this); dlg.exec(); }, false);
    ai->addSeparator();
    add(ai, tr("Libérer la mémoire des modèles IA"), "", [this] {
        Sd::unloadModel();
        AIBackend::clearCache();
        statusBar()->showMessage(tr("Modèles IA déchargés de la mémoire."), 4000);
    }, false);

    // ---------------------------------------------------------------- Affichage
    QMenu* v = menuBar()->addMenu(tr("&Affichage"));
    auto* zi = add(v, tr("Zoom avant"), "", [this] { if (view()) view()->zoomIn(); }, false);
    zi->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    add(v, tr("Zoom arrière"), "Ctrl+-", [this] { if (view()) view()->zoomOut(); }, false);
    add(v, tr("Ajuster à l'écran"), "Ctrl+0", [this] { if (view()) view()->zoomFit(); }, false);
    add(v, tr("Zoom 100 %"), "Ctrl+1", [this] { if (view()) view()->zoom100(); }, false);
    v->addSeparator();
    auto* grid = add(v, tr("Grille"), "Ctrl+'", [this] { if (view()) { view()->showGrid = !view()->showGrid; view()->refresh(); } }, false);
    grid->setCheckable(true);
    auto* edges = add(v, tr("Bords de la sélection"), "Ctrl+H", [this] { if (view()) { view()->showEdges = !view()->showEdges; view()->refresh(); } }, false);
    edges->setCheckable(true); edges->setChecked(true);
    v->addSeparator();
    add(v, tr("Plein écran"), "F", [this] { isFullScreen() ? showNormal() : showFullScreen(); }, false);
    add(v, tr("Afficher / masquer les panneaux"), "Tab", [this] { togglePanels(); }, false);

    // ---------------------------------------------------------------- Fenêtre / Aide
    QMenu* w = menuBar()->addMenu(tr("Fe&nêtre"));
    for (QDockWidget* d : m_docks) w->addAction(d->toggleViewAction());
    QMenu* h = menuBar()->addMenu(tr("&Aide"));
    add(h, tr("Raccourcis clavier"), "F1", [this] { showShortcuts(); }, false);
    add(h, tr("À propos"), "", [this] {
        QMessageBox::about(this, tr("À propos"), tr("<b>PhotoClone</b> — éditeur d'images Qt5 / OpenCV inspiré de Photoshop.<br>Calques, masques, sélections, filtres, réglages, texte, historique."));
    });
}

// ------------------------------------------------------------------------------------------ documents
CanvasView* MainWindow::addDocument(Document* d) {
    auto* v = new CanvasView(d);
    d->setParent(v);
    int i = m_tabs->addTab(v, d->title());
    connect(v, &CanvasView::zoomChanged, this, [this, v](double z) { if (v == view()) m_zoomLabel->setText(QString::number(z * 100, 'f', z >= 1 ? 0 : 1) + " %"); });
    connect(v, &CanvasView::cursorMoved, this, [this, v, d](QPoint p, bool inside) {
        if (v != view()) return;
        if (!inside) { m_posLabel->setText(""); return; }
        auto px = d->composite().at<cv::Vec4b>(p.y(), p.x());
        m_posLabel->setText(tr("X %1  Y %2   RVB %3, %4, %5").arg(p.x()).arg(p.y()).arg(px[2]).arg(px[1]).arg(px[0]));
    });
    connect(d, &Document::selectionChanged, this, [this, d] {
        if (d != doc()) return;
        if (d->hasSelection()) { cv::Rect r = Sel::bounds(d->selection()); m_sizeLabel->setText(tr("Sél. %1 × %2").arg(r.width).arg(r.height)); }
        else m_sizeLabel->setText(QString("%1 × %2 px").arg(d->size().width()).arg(d->size().height()));
    });
    connect(d, &Document::sizeChanged, this, [this, d] { if (d == doc()) m_sizeLabel->setText(QString("%1 × %2 px").arg(d->size().width()).arg(d->size().height())); });
    connect(d->undoStack(), &QUndoStack::cleanChanged, this, [this] { updateTabTitles(); });
    m_tabs->setCurrentIndex(i);
    updateTabTitles();
    return v;
}

void MainWindow::onTabChanged(int idx) {
    if (WS().tools()->current()) WS().tools()->current()->cancel();
    auto* v = qobject_cast<CanvasView*>(m_tabs->widget(idx));
    WS().setView(v);
    Document* d = v ? v->document() : nullptr;
    m_layers->setDocument(d); m_history->setDocument(d); m_histogram->setDocument(d);
    if (d) {
        m_sizeLabel->setText(QString("%1 × %2 px").arg(d->size().width()).arg(d->size().height()));
        m_zoomLabel->setText(QString::number(v->zoom() * 100, 'f', v->zoom() >= 1 ? 0 : 1) + " %");
        v->updateToolCursor(); v->setFocus();
        setWindowTitle(d->title() + " — PhotoClone");
    } else { m_sizeLabel->clear(); setWindowTitle("PhotoClone"); }
}

void MainWindow::updateTabTitles() {
    if (m_closing) return;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* v = qobject_cast<CanvasView*>(m_tabs->widget(i)))
            m_tabs->setTabText(i, v->document()->title() + (v->document()->isModified() ? " *" : ""));
    if (doc()) setWindowTitle(doc()->title() + (doc()->isModified() ? " *" : "") + " — PhotoClone");
}

bool MainWindow::maybeSave(CanvasView* v) {
    Document* d = v->document();
    if (!d->isModified()) return true;
    m_tabs->setCurrentWidget(v);
    auto r = QMessageBox::question(this, tr("Document modifié"), tr("Enregistrer les modifications de « %1 » ?").arg(d->title()),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    return r == QMessageBox::Discard || saveDoc(d, false);
}

void MainWindow::closeTab(int idx) {
    auto* v = qobject_cast<CanvasView*>(m_tabs->widget(idx));
    if (!v || !maybeSave(v)) return;
    if (WS().tools()->current()) WS().tools()->current()->cancel();
    m_tabs->removeTab(idx);
    if (!m_tabs->count()) onTabChanged(-1);
    v->deleteLater();
}

void MainWindow::newDocument() {
    QSize sug;
    QImage clip = QGuiApplication::clipboard()->image();
    if (!clip.isNull()) sug = clip.size();
    NewDocumentDialog dlg(this, sug);
    if (dlg.exec() != QDialog::Accepted) return;
    addDocument(Ops::makeDocument(dlg.docSize(), dlg.background(), WS().bg));
}

void MainWindow::openDialog() {
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Ouvrir"), QString(), ImageIO::openFilter());
    for (const QString& f : files) openPath(f);
}

void MainWindow::openPath(const QString& path) {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* v = qobject_cast<CanvasView*>(m_tabs->widget(i)))
            if (!v->document()->path.isEmpty() && QFileInfo(v->document()->path) == QFileInfo(path)) { m_tabs->setCurrentIndex(i); return; }
    QString err;
    Document* d = ImageIO::open(path, &err);
    if (!d) { QMessageBox::warning(this, tr("Ouverture impossible"), QString("%1\n\n%2").arg(path, err)); return; }
    addDocument(d);
    addRecent(path);
}

void MainWindow::addRecent(const QString& path) {
    QSettings s("PhotoClone", "PhotoClone");
    QStringList r = s.value("recent").toStringList();
    r.removeAll(path); r.prepend(path);
    while (r.size() > 12) r.removeLast();
    s.setValue("recent", r);
}

bool MainWindow::saveDoc(Document* d, bool saveAs) {
    QString path = d->path;
    bool multi = d->layers().size() > 1;
    if (saveAs || path.isEmpty() || (!ImageIO::isNativeProject(path) && multi)) {
        QString sel;
        QString base = path.isEmpty() ? QDir::homePath() + "/" + tr("sans_titre") : QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName();
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer sous"), base + (multi ? ".pcl" : ".png"), ImageIO::saveFilter(), &sel);
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += sel.contains("PNG") ? ".png" : sel.contains("JPEG") ? ".jpg" : sel.contains("TIFF") ? ".tif" : sel.contains("WebP") ? ".webp" : sel.contains("BMP") ? ".bmp" : ".pcl";
    }
    int q = 92;
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "webp") { bool ok; q = Dlg::getInt(this, tr("Qualité"), tr("Qualité (1-100) :"), 92, 1, 100, &ok); if (!ok) return false; }
    QString err;
    if (!ImageIO::save(d, path, &err, q)) { QMessageBox::warning(this, tr("Échec de l'enregistrement"), err); return false; }
    if (!ImageIO::isNativeProject(path)) {
        if (multi) statusBar()->showMessage(tr("Image aplatie exportée. Utilisez le format .pcl pour conserver les calques."), 6000);
        else { d->path = path; d->undoStack()->setClean(); }
    }
    addRecent(path);
    updateTabTitles();
    return true;
}

void MainWindow::exportAs() {
    Document* d = doc();
    if (!d) return;
    QString sel;
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter sous"), QDir::homePath() + "/export.png", "PNG (*.png);;JPEG (*.jpg *.jpeg);;TIFF (*.tif);;WebP (*.webp);;BMP (*.bmp)", &sel);
    if (path.isEmpty()) return;
    int q = 92; QString err;
    if (!ImageIO::save(d, path, &err, q)) QMessageBox::warning(this, tr("Échec de l'export"), err);
    else statusBar()->showMessage(tr("Exporté : %1").arg(path), 5000);
}

// ------------------------------------------------------------------------------------------ actions
void MainWindow::runEffect(const QString& id, bool reuse, const QVariantMap& overrides) {
    Document* d = doc();
    if (!d) return;
    auto e = EffectRegistry::instance().find(id);
    if (!e) return;
    QString why;
    if (!d->editableLayer(&why)) { statusBar()->showMessage(why, 4000); return; }
    if (e->requiresSelection && !d->hasSelection()) { statusBar()->showMessage(tr("%1 : sélectionnez d'abord une zone.").arg(e->name.section(QStringLiteral("…"), 0, 0)), 4000); return; }
    Params p = (reuse && m_lastEffect == e) ? m_lastParams : e->defaults();
    if (id == "filter.clouds" && !reuse) { p.set("fg", WS().fg); p.set("bg", WS().bg); }
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) p.set(it.key(), it.value());
    if (!e->defs.empty()) {
        EffectDialog dlg(d, e, p, this);
        if (dlg.exec() != QDialog::Accepted) return;
        p = dlg.params();
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    Ops::applyEffect(d, *e, p);
    QApplication::restoreOverrideCursor();
    m_lastEffect = e; m_lastParams = p;
}

// ------------------------------------------------------------------------------------------ IA
bool MainWindow::ensureAIModel(AIArchitecture a) {
    const AIArchitectureInfo& inf = AIModels::info(a);
    if (!AIModels::libraryAvailable()) {
        QMessageBox::information(this, tr("Fonction IA indisponible"),
            tr("Cette version de PhotoClone a été compilée sans la bibliothèque vision.cpp.\nVoir depend/visioncpp/BUILD_FROM_SOURCE.md."));
        return false;
    }
    auto problem = [&]() -> QString {
        if (!AIModels::isConfigured(a)) return tr("aucun modèle configuré");
        return AIBackend::validateModelFile(a, AIModels::modelPath(a));
    };
    QString why = problem();
    if (why.isEmpty()) return true;
    auto r = QMessageBox::question(this, tr("Modèle IA requis"),
        tr("%1 — %2 : %3.\n\nOuvrir les réglages des modèles maintenant ?").arg(inf.name, inf.task, why));
    if (r == QMessageBox::Yes) { AIModelsDialog dlg(this); dlg.exec(); }
    return problem().isEmpty();
}

void MainWindow::cmdRemoveBackground() {
    Document* d = doc();
    if (!d) return;
    QString why;
    if (!d->editableLayer(&why)) { statusBar()->showMessage(why, 4000); return; }
    if (!ensureAIModel(AIArchitecture::BiRefNet)) return;
    RemoveBackgroundDialog dlg(d, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.hasMask()) return;
    QString err, warn;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = Ops::removeBackground(d, dlg.rawMask(), dlg.params(), &err, &warn);
    QApplication::restoreOverrideCursor();
    if (!ok) QMessageBox::warning(this, tr("Suppression de l'arrière-plan"), err);
    else if (!warn.isEmpty()) statusBar()->showMessage(warn, 8000);
}

void MainWindow::cmdDepth() {
    Document* d = doc();
    if (!d || !d->activeLayer()) return;
    if (!ensureAIModel(AIArchitecture::DepthAnything)) return;
    DepthDialog dlg(d, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.hasDepth()) return;
    QString err;
    if (!Ops::applyDepth(d, dlg.rawDepth(), dlg.params(), &err)) QMessageBox::warning(this, tr("Carte de profondeur"), err);
}

void MainWindow::cmdUpscale() {
    Document* d = doc();
    if (!d) return;
    if (!ensureAIModel(AIArchitecture::Esrgan)) return;
    UpscaleDialog dlg(d, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = Ops::aiUpscale(d, dlg.params(), &err, [this](int i, int n) {
        statusBar()->showMessage(tr("Agrandissement IA : calque %1 sur %2 (peut être long sur CPU)…").arg(i + 1).arg(n));
        QApplication::processEvents();
        return true;
    });
    QApplication::restoreOverrideCursor();
    statusBar()->clearMessage();
    if (!ok) QMessageBox::warning(this, tr("Agrandissement IA"), err);
    else if (view()) view()->zoomFit(true);
}

// ------------------------------------------------------------------------------------------ Stable Diffusion
bool MainWindow::ensureSd() {
    if (!Sd::libraryCompiled()) {
        QMessageBox::information(this, tr("Stable Diffusion indisponible"),
            tr("Cette version de PhotoClone a été compilée sans stable-diffusion.cpp.\nVoir depend/stablediffusioncpp/BUILD_FROM_SOURCE.md."));
        return false;
    }
    QString libErr;
    if (!Sd::libraryLoaded(&libErr)) { QMessageBox::warning(this, tr("Stable Diffusion indisponible"), libErr); return false; }
    auto problem = [] { return Sd::validateConfig(Sd::loadConfig()); };
    QString why = problem();
    if (why.isEmpty()) return true;
    auto r = QMessageBox::question(this, tr("Modèle Stable Diffusion requis"), tr("%1\n\nOuvrir les réglages maintenant ?").arg(why));
    if (r == QMessageBox::Yes) { SdSettingsDialog dlg(this); dlg.exec(); }
    return problem().isEmpty();
}

void MainWindow::cmdSdGenerate() {
    if (!ensureSd()) return;
    Document* d = doc();
    SdGenerateDialog dlg(d, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const cv::Mat img = dlg.selectedImage();
    if (img.empty()) return;
    if (!d) { addDocument(Ops::sdNewDocument(img, dlg.promptText())); return; }
    QString err;
    if (!Ops::sdAddImage(d, img, dlg.placement(), dlg.promptText(), &err)) QMessageBox::warning(this, tr("Génération d'image"), err);
}

void MainWindow::cmdSdInpaint() {
    Document* d = doc();
    if (!d) return;
    QString why;
    if (!d->editableLayer(&why)) { statusBar()->showMessage(why, 4000); return; }
    if (!d->hasSelection()) { statusBar()->showMessage(tr("Inpainting : sélectionnez d'abord la zone à régénérer."), 5000); return; }
    if (!ensureSd()) return;
    SdInpaintDialog dlg(d, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const cv::Mat gen = dlg.selectedImage();
    if (gen.empty()) return;
    QString err;
    if (!Ops::sdApplyInpaint(d, dlg.plan(), gen, &err)) QMessageBox::warning(this, tr("Inpainting"), err);
}

void MainWindow::repeatLastFilter() {
    Document* d = doc();
    if (!d || !m_lastEffect) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    Ops::applyEffect(d, *m_lastEffect, m_lastParams);
    QApplication::restoreOverrideCursor();
}

void MainWindow::doPaste(bool inPlace) {
    QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) { statusBar()->showMessage(tr("Le presse-papiers ne contient pas d'image."), 3000); return; }
    if (!doc()) addDocument(Ops::makeDocument(img.size(), 0, Qt::white));
    QPoint c = view() ? view()->viewCenterImage().toPoint() : QPoint(0, 0);
    if (Ops::paste(doc(), inPlace, c)) WS().tools()->select(WS().tools()->byId("move"));
}

void MainWindow::startTransform() {
    Document* d = doc();
    if (!d) return;
    auto* tt = WS().tools()->transform();
    if (tt->active()) return;
    if (tt->begin(d)) WS().tools()->select(tt);
}

void MainWindow::modifySelectionPrompt(int kind, const QString& title, const QString& label, int def, int max) {
    Document* d = doc();
    if (!d || !d->hasSelection()) { statusBar()->showMessage(tr("Aucune sélection."), 3000); return; }
    bool ok = false;
    int v = Dlg::getInt(this, title, label, def, 1, max, &ok);
    if (ok) Ops::modifySelection(d, kind, v);
}

void MainWindow::togglePanels() {
    m_panelsHidden = !m_panelsHidden;
    for (QDockWidget* d : m_docks) d->setVisible(!m_panelsHidden);
    m_toolbox->setVisible(!m_panelsHidden);
    m_options->setVisible(!m_panelsHidden);
}

void MainWindow::showShortcuts() {
    QMessageBox::information(this, tr("Raccourcis clavier"),
        tr("<table cellpadding=3>"
        "<tr><td><b>V</b> Déplacement</td><td><b>M</b> Sélection (Maj+M : ellipse, puis transformation de la sélection)</td><td><b>L</b> Lasso (Maj+L)</td></tr>"
        "<tr><td><b>W</b> Baguette magique</td><td><b>C</b> Recadrage</td><td><b>I</b> Pipette</td></tr>"
        "<tr><td><b>B</b> Pinceau (Maj+B : crayon)</td><td><b>S</b> Tampon</td><td><b>E</b> Gomme</td></tr>"
        "<tr><td><b>G</b> Dégradé (Maj+G : pot)</td><td><b>R</b> Goutte/Netteté/Doigt</td><td><b>O</b> Densité -/+</td></tr>"
        "<tr><td><b>T</b> Texte</td><td><b>U</b> Formes</td><td><b>H</b> Main / <b>Z</b> Zoom</td></tr>"
        "<tr><td><b>X</b> permuter couleurs</td><td><b>D</b> couleurs par défaut</td><td><b>[ ]</b> taille du pinceau</td></tr>"
        "<tr><td><b>Espace</b> main temporaire</td><td><b>Alt+molette</b> zoom</td><td><b>Ctrl+0 / Ctrl+1</b> ajuster / 100 %</td></tr>"
        "<tr><td><b>Ctrl+Z</b> annuler</td><td><b>Ctrl+Maj+Z</b> rétablir</td><td><b>Ctrl+T</b> transformation</td></tr>"
        "<tr><td><b>Ctrl+A/D</b> tout / désélectionner</td><td><b>Ctrl+Maj+I</b> inverser sél.</td><td><b>Ctrl+J</b> dupliquer / calque par copie</td></tr>"
        "<tr><td><b>Ctrl+L/M/U/B</b> niveaux/courbes/teinte/balance</td><td><b>Ctrl+I</b> négatif</td><td><b>Ctrl+F</b> dernier filtre</td></tr>"
        "<tr><td><b>Alt+Retour arr.</b> remplir PP</td><td><b>Ctrl+Retour arr.</b> remplir AP</td><td><b>Maj+F5</b> remplissage d'après le contenu</td></tr>"
        "<tr><td><b>Tab</b> panneaux</td><td><b>Ctrl+Alt+K</b> supprimer l'arrière-plan (IA)</td><td><b>Maj+W</b> sélection par IA (MobileSAM)</td></tr>"
        "<tr><td><b>Ctrl+Alt+G</b> générer une image (Stable Diffusion)</td><td><b>Ctrl+Alt+P</b> inpainting sur la sélection</td><td></td></tr>"
        "</table>"));
}

// ------------------------------------------------------------------------------------------ événements fenêtre
void MainWindow::closeEvent(QCloseEvent* e) {
    if (Sd::isBusy()) {              // un calcul Stable Diffusion tourne encore : l'annuler et attendre la fin de l'étape en cours
        QApplication::setOverrideCursor(Qt::WaitCursor);
        Sd::shutdown();
        QApplication::restoreOverrideCursor();
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* v = qobject_cast<CanvasView*>(m_tabs->widget(i))) if (!maybeSave(v)) { e->ignore(); return; }
    QSettings s("PhotoClone", "PhotoClone");
    s.setValue("geometry", saveGeometry());
    s.setValue("state", saveState());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) openPath(u.toLocalFile());
}
