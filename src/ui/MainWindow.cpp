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
    auto* dColor = mk("Couleur", "dockColor", m_color);
    auto* dSw = mk("Nuancier", "dockSwatches", m_swatches);
    auto* dHist = mk("Histogramme", "dockHistogram", m_histogram);
    auto* dLay = mk("Calques", "dockLayers", m_layers);
    auto* dHis = mk("Historique", "dockHistory", m_history);
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
    statusBar()->showMessage("Ctrl+N : nouveau document • Ctrl+O : ouvrir • Espace : main • Alt+molette : zoom", 8000);
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
    QMenu* f = menuBar()->addMenu("&Fichier");
    add(f, "Nouveau…", "Ctrl+N", [this] { newDocument(); });
    add(f, "Ouvrir…", "Ctrl+O", [this] { openDialog(); });
    m_recentMenu = f->addMenu("Ouvrir un fichier récent");
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        for (const QString& p : QSettings("PhotoClone", "PhotoClone").value("recent").toStringList())
            m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] { openPath(p); });
        if (m_recentMenu->isEmpty()) m_recentMenu->addAction("(vide)")->setEnabled(false);
    });
    f->addSeparator();
    add(f, "Fermer", "Ctrl+W", [this] { if (m_tabs->count()) closeTab(m_tabs->currentIndex()); });
    add(f, "Tout fermer", "Ctrl+Alt+W", [this] { while (m_tabs->count()) { int n = m_tabs->count(); closeTab(m_tabs->count() - 1); if (m_tabs->count() == n) break; } });
    f->addSeparator();
    add(f, "Enregistrer", "Ctrl+S", onDoc([this](Document* d) { saveDoc(d, false); }));
    add(f, "Enregistrer sous…", "Ctrl+Shift+S", onDoc([this](Document* d) { saveDoc(d, true); }));
    add(f, "Exporter sous (image aplatie)…", "Ctrl+Alt+Shift+W", [this] { exportAs(); });
    f->addSeparator();
    add(f, "Quitter", "Ctrl+Q", [this] { close(); });

    // ---------------------------------------------------------------- Édition
    QMenu* e = menuBar()->addMenu("&Édition");
    add(e, "Annuler", "Ctrl+Z", onDoc([](Document* d) { d->undoStack()->undo(); }));
    add(e, "Rétablir", "Ctrl+Shift+Z", onDoc([](Document* d) { d->undoStack()->redo(); }));
    add(e, "Pas en arrière", "Ctrl+Alt+Z", onDoc([](Document* d) { d->undoStack()->undo(); }));
    e->addSeparator();
    add(e, "Couper", "Ctrl+X", onDoc([](Document* d) { Ops::cut(d); }));
    add(e, "Copier", "Ctrl+C", onDoc([](Document* d) { Ops::copy(d, false); }));
    add(e, "Copier avec fusion", "Ctrl+Shift+C", onDoc([](Document* d) { Ops::copy(d, true); }));
    add(e, "Coller", "Ctrl+V", [this] { doPaste(false); });
    add(e, "Coller sur place", "Ctrl+Shift+V", [this] { doPaste(true); });
    add(e, "Effacer", "Delete", onDoc([](Document* d) { Ops::clearSelection(d); }));
    e->addSeparator();
    add(e, "Remplir avec la couleur de premier plan", "Alt+Backspace", onDoc([](Document* d) { Ops::fillSelection(d, WS().fg, "Remplir (premier plan)"); }));
    add(e, "Remplir avec la couleur d'arrière-plan", "Ctrl+Backspace", onDoc([](Document* d) { Ops::fillSelection(d, WS().bg, "Remplir (arrière-plan)"); }));
    e->addSeparator();
    add(e, "Transformation manuelle", "Ctrl+T", [this] { startTransform(); });
    QMenu* tr = e->addMenu("Transformation du calque");
    add(tr, "Miroir horizontal", "", onDoc([](Document* d) { Ops::flipLayer(d, true); }));
    add(tr, "Miroir vertical", "", onDoc([](Document* d) { Ops::flipLayer(d, false); }));

    // ---------------------------------------------------------------- Image
    QMenu* im = menuBar()->addMenu("&Image");
    auto& reg = EffectRegistry::instance();
    QMenu* adj = im->addMenu("Réglages");
    static const QMap<QString, QString> adjKeys = {
        {"adjust.levels", "Ctrl+L"}, {"adjust.curves", "Ctrl+M"}, {"adjust.huesat", "Ctrl+U"}, {"adjust.colorbalance", "Ctrl+B"},
        {"adjust.bw", "Ctrl+Alt+Shift+B"}, {"adjust.invert", "Ctrl+I"}, {"adjust.desaturate", "Ctrl+Shift+U"}, {"adjust.autotone", "Ctrl+Shift+L"},
        {"adjust.autocontrast", "Ctrl+Alt+Shift+L"}, {"adjust.autocolor", "Ctrl+Shift+B"}};
    QMenu* autoMenu = nullptr;
    for (const EffectPtr& ef : reg.all()) {
        if (!ef->id.startsWith("adjust.")) continue;
        QMenu* target = adj;
        if (ef->category == "Auto") { if (!autoMenu) { adj->addSeparator(); autoMenu = adj; } }
        QString id = ef->id;
        add(target, ef->name, adjKeys.value(id), [this, id] { runEffect(id); });
    }
    im->addSeparator();
    add(im, "Taille de l'image…", "Ctrl+Alt+I", onDoc([this](Document* d) { ImageSizeDialog dlg(this, d->size()); if (dlg.exec() == QDialog::Accepted) Ops::resizeImage(d, dlg.newSize(), dlg.interpolation()); }));
    add(im, "Taille de la zone de travail…", "Ctrl+Alt+C", onDoc([this](Document* d) { CanvasSizeDialog dlg(this, d->size()); if (dlg.exec() == QDialog::Accepted) Ops::resizeCanvas(d, dlg.newSize(), dlg.anchor()); }));
    QMenu* rot = im->addMenu("Rotation de la zone de travail");
    add(rot, "180°", "", onDoc([](Document* d) { Ops::rotateImage(d, 180); }));
    add(rot, "90° horaire", "", onDoc([](Document* d) { Ops::rotateImage(d, 90); }));
    add(rot, "90° anti-horaire", "", onDoc([](Document* d) { Ops::rotateImage(d, 270); }));
    rot->addSeparator();
    add(rot, "Miroir horizontal", "", onDoc([](Document* d) { Ops::flipImage(d, true); }));
    add(rot, "Miroir vertical", "", onDoc([](Document* d) { Ops::flipImage(d, false); }));
    add(im, "Recadrer selon la sélection", "", onDoc([](Document* d) {
        if (d->hasSelection()) Ops::cropTo(d, mu::toQt(Sel::bounds(d->selection())));
        else WS().message("Aucune sélection : utilisez l'outil Recadrage (C).");
    }));

    // ---------------------------------------------------------------- Calque
    QMenu* l = menuBar()->addMenu("&Calque");
    add(l, "Nouveau calque", "Ctrl+Shift+N", onDoc([](Document* d) { Ops::addLayer(d); }));
    add(l, "Calque par copier / Dupliquer", "Ctrl+J", onDoc([](Document* d) { Ops::layerViaCopy(d, false); }));
    add(l, "Calque par couper", "Ctrl+Shift+J", onDoc([](Document* d) { Ops::layerViaCopy(d, true); }));
    add(l, "Supprimer le calque", "", onDoc([](Document* d) { Ops::deleteLayer(d); }));
    l->addSeparator();
    QMenu* mk = l->addMenu("Masque de fusion");
    add(mk, "Tout faire apparaître / selon la sélection", "", onDoc([](Document* d) { Ops::addMask(d, false); }));
    add(mk, "Tout masquer", "", onDoc([](Document* d) { Ops::addMask(d, true); }));
    add(mk, "Appliquer le masque", "", onDoc([](Document* d) { Ops::deleteMask(d, true); }));
    add(mk, "Supprimer le masque", "", onDoc([](Document* d) { Ops::deleteMask(d, false); }));
    add(l, "Pixelliser le texte", "", onDoc([](Document* d) { Ops::rasterizeText(d); }));
    l->addSeparator();
    QMenu* ar = l->addMenu("Disposition");
    add(ar, "Premier plan", "Ctrl+Shift+]", onDoc([](Document* d) { Ops::moveLayerToEnd(d, true); }));
    add(ar, "Avancer", "Ctrl+]", onDoc([](Document* d) { Ops::moveLayer(d, 1); }));
    add(ar, "Reculer", "Ctrl+[", onDoc([](Document* d) { Ops::moveLayer(d, -1); }));
    add(ar, "Arrière-plan", "Ctrl+Shift+[", onDoc([](Document* d) { Ops::moveLayerToEnd(d, false); }));
    l->addSeparator();
    add(l, "Fusionner vers le bas", "Ctrl+E", onDoc([](Document* d) { Ops::mergeDown(d); }));
    add(l, "Fusionner les calques visibles", "Ctrl+Shift+E", onDoc([](Document* d) { Ops::mergeVisible(d); }));
    add(l, "Aplatir l'image", "", onDoc([](Document* d) { Ops::flatten(d); }));

    // ---------------------------------------------------------------- Sélection
    QMenu* s = menuBar()->addMenu("&Sélection");
    add(s, "Tout sélectionner", "Ctrl+A", onDoc([](Document* d) { Ops::selectAll(d); }));
    add(s, "Désélectionner", "Ctrl+D", onDoc([](Document* d) { Ops::deselect(d); }));
    add(s, "Resélectionner", "Ctrl+Shift+D", onDoc([](Document* d) { d->reselect(); }));
    add(s, "Intervertir la sélection", "Ctrl+Shift+I", onDoc([](Document* d) { Ops::invertSelection(d); }));
    add(s, "Transformer la sélection (agrandir / pivoter)", "", [this] {
        auto* tm = WS().tools(); Tool* t = tm->byId("sel_transform");
        if (tm->current() == t) t->activate(); else tm->select(t);
    });
    s->addSeparator();
    QMenu* mod = s->addMenu("Modifier");
    add(mod, "Contour progressif…", "Shift+F6", [this] { modifySelectionPrompt(0, "Contour progressif", "Rayon (px) :", 5, 250); });
    add(mod, "Agrandir…", "", [this] { modifySelectionPrompt(1, "Agrandir la sélection", "De (px) :", 5, 500); });
    add(mod, "Contracter…", "", [this] { modifySelectionPrompt(2, "Contracter la sélection", "De (px) :", 5, 500); });
    add(mod, "Lisser…", "", [this] { modifySelectionPrompt(3, "Lisser la sélection", "Rayon (px) :", 5, 100); });

    // ---------------------------------------------------------------- Filtre
    QMenu* fl = menuBar()->addMenu("Fi&ltre");
    add(fl, "Dernier filtre", "Ctrl+F", [this] { repeatLastFilter(); });
    add(fl, "Dernier filtre (avec options)…", "Ctrl+Alt+F", [this] { if (m_lastEffect) runEffect(m_lastEffect->id, true); });
    fl->addSeparator();
    for (const QString& cat : reg.categories("filter")) {
        QMenu* sub = fl->addMenu(cat);
        for (const EffectPtr& ef : reg.all())
            if (ef->id.startsWith("filter.") && ef->category == cat) { QString id = ef->id; add(sub, ef->name, "", [this, id] { runEffect(id); }); }
    }

    // ---------------------------------------------------------------- Affichage
    QMenu* v = menuBar()->addMenu("&Affichage");
    auto* zi = add(v, "Zoom avant", "", [this] { if (view()) view()->zoomIn(); }, false);
    zi->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    add(v, "Zoom arrière", "Ctrl+-", [this] { if (view()) view()->zoomOut(); }, false);
    add(v, "Ajuster à l'écran", "Ctrl+0", [this] { if (view()) view()->zoomFit(); }, false);
    add(v, "Zoom 100 %", "Ctrl+1", [this] { if (view()) view()->zoom100(); }, false);
    v->addSeparator();
    auto* grid = add(v, "Grille", "Ctrl+'", [this] { if (view()) { view()->showGrid = !view()->showGrid; view()->refresh(); } }, false);
    grid->setCheckable(true);
    auto* edges = add(v, "Bords de la sélection", "Ctrl+H", [this] { if (view()) { view()->showEdges = !view()->showEdges; view()->refresh(); } }, false);
    edges->setCheckable(true); edges->setChecked(true);
    v->addSeparator();
    add(v, "Plein écran", "F", [this] { isFullScreen() ? showNormal() : showFullScreen(); }, false);
    add(v, "Afficher / masquer les panneaux", "Tab", [this] { togglePanels(); }, false);

    // ---------------------------------------------------------------- Fenêtre / Aide
    QMenu* w = menuBar()->addMenu("Fe&nêtre");
    for (QDockWidget* d : m_docks) w->addAction(d->toggleViewAction());
    QMenu* h = menuBar()->addMenu("&Aide");
    add(h, "Raccourcis clavier", "F1", [this] { showShortcuts(); }, false);
    add(h, "À propos", "", [this] {
        QMessageBox::about(this, "À propos", "<b>PhotoClone</b> — éditeur d'images Qt5 / OpenCV inspiré de Photoshop.<br>Calques, masques, sélections, filtres, réglages, texte, historique.");
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
        m_posLabel->setText(QString("X %1  Y %2   RVB %3, %4, %5").arg(p.x()).arg(p.y()).arg(px[2]).arg(px[1]).arg(px[0]));
    });
    connect(d, &Document::selectionChanged, this, [this, d] {
        if (d != doc()) return;
        if (d->hasSelection()) { cv::Rect r = Sel::bounds(d->selection()); m_sizeLabel->setText(QString("Sél. %1 × %2").arg(r.width).arg(r.height)); }
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
    auto r = QMessageBox::question(this, "Document modifié", QString("Enregistrer les modifications de « %1 » ?").arg(d->title()),
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
    QStringList files = QFileDialog::getOpenFileNames(this, "Ouvrir", QString(), ImageIO::openFilter());
    for (const QString& f : files) openPath(f);
}

void MainWindow::openPath(const QString& path) {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* v = qobject_cast<CanvasView*>(m_tabs->widget(i)))
            if (!v->document()->path.isEmpty() && QFileInfo(v->document()->path) == QFileInfo(path)) { m_tabs->setCurrentIndex(i); return; }
    QString err;
    Document* d = ImageIO::open(path, &err);
    if (!d) { QMessageBox::warning(this, "Ouverture impossible", QString("%1\n\n%2").arg(path, err)); return; }
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
        QString base = path.isEmpty() ? QDir::homePath() + "/" + "sans_titre" : QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName();
        path = QFileDialog::getSaveFileName(this, "Enregistrer sous", base + (multi ? ".pcl" : ".png"), ImageIO::saveFilter(), &sel);
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += sel.contains("PNG") ? ".png" : sel.contains("JPEG") ? ".jpg" : sel.contains("TIFF") ? ".tif" : sel.contains("WebP") ? ".webp" : sel.contains("BMP") ? ".bmp" : ".pcl";
    }
    int q = 92;
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "webp") { bool ok; q = Dlg::getInt(this, "Qualité", "Qualité (1-100) :", 92, 1, 100, &ok); if (!ok) return false; }
    QString err;
    if (!ImageIO::save(d, path, &err, q)) { QMessageBox::warning(this, "Échec de l'enregistrement", err); return false; }
    if (!ImageIO::isNativeProject(path)) {
        if (multi) statusBar()->showMessage("Image aplatie exportée. Utilisez le format .pcl pour conserver les calques.", 6000);
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
    QString path = QFileDialog::getSaveFileName(this, "Exporter sous", QDir::homePath() + "/export.png", "PNG (*.png);;JPEG (*.jpg *.jpeg);;TIFF (*.tif);;WebP (*.webp);;BMP (*.bmp)", &sel);
    if (path.isEmpty()) return;
    int q = 92; QString err;
    if (!ImageIO::save(d, path, &err, q)) QMessageBox::warning(this, "Échec de l'export", err);
    else statusBar()->showMessage("Exporté : " + path, 5000);
}

// ------------------------------------------------------------------------------------------ actions
void MainWindow::runEffect(const QString& id, bool reuse) {
    Document* d = doc();
    if (!d) return;
    auto e = EffectRegistry::instance().find(id);
    if (!e) return;
    QString why;
    if (!d->editableLayer(&why)) { statusBar()->showMessage(why, 4000); return; }
    Params p = (reuse && m_lastEffect == e) ? m_lastParams : e->defaults();
    if (id == "filter.clouds" && !reuse) { p.set("fg", WS().fg); p.set("bg", WS().bg); }
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

void MainWindow::repeatLastFilter() {
    Document* d = doc();
    if (!d || !m_lastEffect) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    Ops::applyEffect(d, *m_lastEffect, m_lastParams);
    QApplication::restoreOverrideCursor();
}

void MainWindow::doPaste(bool inPlace) {
    QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) { statusBar()->showMessage("Le presse-papiers ne contient pas d'image.", 3000); return; }
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
    if (!d || !d->hasSelection()) { statusBar()->showMessage("Aucune sélection.", 3000); return; }
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
    QMessageBox::information(this, "Raccourcis clavier",
        "<table cellpadding=3>"
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
        "<tr><td><b>Alt+Retour arr.</b> remplir PP</td><td><b>Ctrl+Retour arr.</b> remplir AP</td><td><b>Tab</b> panneaux</td></tr>"
        "</table>");
}

// ------------------------------------------------------------------------------------------ événements fenêtre
void MainWindow::closeEvent(QCloseEvent* e) {
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
