#include "Panels.h"
#include "Dialogs.h"
#include "Icons.h"
#include "Widgets.h"
#include "core/MatUtil.h"
#include "core/Operations.h"
#include "core/Workspace.h"
#include <opencv2/imgproc.hpp>
#include <QCheckBox>
#include <QHBoxLayout>
#include "MovableDialog.h"
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

// ============================================================================ Calques
LayersPanel::LayersPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    m_blend = new QComboBox; m_blend->addItems(blendModeNames());
    m_opacity = new SliderSpin(0, 100, 100, 0);
    m_lock = new QCheckBox("Verrouiller");
    auto* r1 = new QHBoxLayout; r1->addWidget(m_blend, 1); r1->addWidget(m_lock);
    auto* r2 = new QHBoxLayout; r2->addWidget(new QLabel("Opacité :")); r2->addWidget(m_opacity, 1);
    m_list = new QListWidget;
    m_list->setIconSize(QSize(44, 36));
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSpacing(1);
    auto* bar = new QHBoxLayout;
    auto mk = [&](const QString& icon, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton; b->setIcon(actionIcon(icon)); b->setToolTip(tip); b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [fn] { fn(); });
        bar->addWidget(b);
        return b;
    };
    mk("newlayer", "Nouveau calque (Ctrl+Maj+N)", [this] { if (m_doc) Ops::addLayer(m_doc); });
    mk("duplicate", "Dupliquer le calque (Ctrl+J)", [this] { if (m_doc) Ops::layerViaCopy(m_doc, false); });
    mk("mask", "Ajouter un masque de fusion", [this] { if (m_doc) Ops::addMask(m_doc, false); });
    m_editMask = mk("mask", "Modifier le masque (sinon : les pixels)", [] {});
    m_editMask->setCheckable(true); m_editMask->setText("✎"); m_editMask->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addStretch();
    mk("delete", "Supprimer le calque", [this] { if (m_doc) Ops::deleteLayer(m_doc); });
    v->addLayout(r1); v->addLayout(r2); v->addWidget(m_list, 1); v->addLayout(bar);

    m_thumbTimer.setSingleShot(true); m_thumbTimer.setInterval(250);
    connect(&m_thumbTimer, &QTimer::timeout, this, &LayersPanel::updateThumbs);
    m_commitTimer.setSingleShot(true); m_commitTimer.setInterval(450);
    connect(&m_commitTimer, &QTimer::timeout, this, [this] {
        if (m_doc && m_hasBefore) if (auto l = m_doc->activeLayer()) m_doc->pushLayerChange("Opacité du calque", l, m_before);
        m_hasBefore = false;
    });

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_building || !m_doc || row < 0) return;
        m_doc->setActiveIndex(int(m_doc->layers().size()) - 1 - row);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_building || !m_doc) return;
        if (auto l = layerOf(it)) { bool vis = it->checkState() == Qt::Checked; if (l->props.visible != vis) { l->props.visible = vis; m_doc->invalidateAll(); } }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &LayersPanel::rename);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &LayersPanel::contextMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] { QTimer::singleShot(0, this, &LayersPanel::onReordered); });

    connect(m_blend, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (!m_doc) return;
        if (auto l = m_doc->activeLayer()) { LayerProps p = l->props; p.blend = BlendMode(i); Ops::setProps(m_doc, l, p, "Mode de fusion"); }
    });
    connect(m_lock, &QCheckBox::clicked, this, [this](bool on) {
        if (!m_doc) return;
        if (auto l = m_doc->activeLayer()) { LayerProps p = l->props; p.locked = on; Ops::setProps(m_doc, l, p, on ? "Verrouiller le calque" : "Déverrouiller le calque"); }
    });
    connect(m_opacity, &SliderSpin::valueChanged, this, [this](double val) {
        if (m_building || !m_doc) return;
        auto l = m_doc->activeLayer();
        if (!l) return;
        if (!m_hasBefore) { m_before = l->state(); m_hasBefore = true; }
        l->props.opacity = float(val / 100.0);
        m_doc->invalidateAll();
        m_commitTimer.start();
    });
    connect(m_editMask, &QToolButton::toggled, this, [this](bool on) {
        if (m_building || !m_doc) return;
        if (auto l = m_doc->activeLayer()) { l->editingMask = on && l->hasMask(); rebuild(); }
    });
}

Layer::Ptr LayersPanel::layerOf(QListWidgetItem* it) const { return it && m_doc ? m_doc->layerById(it->data(Qt::UserRole).toInt()) : nullptr; }

void LayersPanel::setDocument(Document* d) {
    if (m_doc) m_doc->disconnect(this);
    m_doc = d;
    if (d) {
        connect(d, &Document::layersChanged, this, &LayersPanel::rebuild);
        connect(d, &Document::activeLayerChanged, this, &LayersPanel::rebuild);
        connect(d, &Document::changed, this, [this] { m_thumbTimer.start(); });
    }
    rebuild();
}

void LayersPanel::rebuild() {
    m_building = true;
    m_list->clear();
    if (m_doc) {
        const auto& L = m_doc->layers();
        for (int i = int(L.size()) - 1; i >= 0; --i) {
            const auto& l = L[i];
            auto* it = new QListWidgetItem(m_list);
            QString label = l->props.name;
            if (l->hasMask()) label += l->editingMask ? "   ▣ (masque actif)" : "   ▣";
            if (l->isText()) label = "T  " + label;
            if (l->props.locked) label += "  🔒";
            it->setText(label);
            it->setIcon(QIcon(QPixmap::fromImage(mu::thumbnail(l->image, QSize(44, 36)))));
            it->setData(Qt::UserRole, l->id());
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
            it->setCheckState(l->props.visible ? Qt::Checked : Qt::Unchecked);
            if (i == m_doc->activeIndex()) m_list->setCurrentItem(it);
        }
    }
    m_building = false;
    syncControls();
}

void LayersPanel::updateThumbs() {
    if (!m_doc) return;
    m_building = true;
    for (int r = 0; r < m_list->count(); ++r)
        if (auto l = layerOf(m_list->item(r))) m_list->item(r)->setIcon(QIcon(QPixmap::fromImage(mu::thumbnail(l->image, QSize(44, 36)))));
    m_building = false;
}

void LayersPanel::syncControls() {
    m_building = true;
    auto l = m_doc ? m_doc->activeLayer() : nullptr;
    setEnabled(m_doc != nullptr);
    if (l) {
        m_blend->setCurrentIndex(int(l->props.blend));
        m_opacity->setValue(l->props.opacity * 100);
        m_lock->setChecked(l->props.locked);
        m_editMask->setEnabled(l->hasMask());
        m_editMask->setChecked(l->editingMask && l->hasMask());
    }
    m_building = false;
}

void LayersPanel::onReordered() {
    if (!m_doc) return;
    std::vector<int> ids;
    for (int r = m_list->count() - 1; r >= 0; --r) ids.push_back(m_list->item(r)->data(Qt::UserRole).toInt());
    Ops::reorderByIds(m_doc, ids);
    rebuild();
}

void LayersPanel::rename(QListWidgetItem* it) {
    auto l = layerOf(it);
    if (!l) return;
    if (l->isText()) { TextData td = *l->text; if (TextDialog::edit(this, td) && !td.text.trimmed().isEmpty()) Ops::commitText(m_doc, l, td); return; }
    bool ok = false;
    QString n = Dlg::getText(this, "Renommer le calque", "Nom :", l->props.name, &ok);
    if (ok && !n.isEmpty()) { LayerProps p = l->props; p.name = n; Ops::setProps(m_doc, l, p, "Renommer le calque"); }
}

void LayersPanel::contextMenu(const QPoint& pos) {
    if (!m_doc) return;
    auto l = m_doc->activeLayer();
    QMenu m;
    m.addAction("Nouveau calque", [this] { Ops::addLayer(m_doc); });
    m.addAction("Dupliquer le calque", [this] { Ops::layerViaCopy(m_doc, false); });
    m.addAction("Supprimer le calque", [this] { Ops::deleteLayer(m_doc); });
    m.addSeparator();
    if (l && !l->hasMask()) { m.addAction("Ajouter un masque (tout révéler)", [this] { Ops::addMask(m_doc, false); }); m.addAction("Ajouter un masque (tout masquer)", [this] { Ops::addMask(m_doc, true); }); }
    if (l && l->hasMask()) {
        m.addAction(l->props.maskEnabled ? "Désactiver le masque" : "Activer le masque", [this, l] { LayerProps p = l->props; p.maskEnabled = !p.maskEnabled; Ops::setProps(m_doc, l, p, "Activer/désactiver le masque"); });
        m.addAction("Appliquer le masque", [this] { Ops::deleteMask(m_doc, true); });
        m.addAction("Supprimer le masque", [this] { Ops::deleteMask(m_doc, false); });
    }
    if (l && l->isText()) m.addAction("Pixelliser le texte", [this] { Ops::rasterizeText(m_doc); });
    m.addSeparator();
    m.addAction("Fusionner vers le bas", [this] { Ops::mergeDown(m_doc); });
    m.addAction("Fusionner les calques visibles", [this] { Ops::mergeVisible(m_doc); });
    m.addAction("Aplatir l'image", [this] { Ops::flatten(m_doc); });
    m.exec(m_list->viewport()->mapToGlobal(pos));
}

// ============================================================================ Couleur
ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    m_target = new QComboBox; m_target->addItems({"Couleur de premier plan", "Couleur d'arrière-plan"});
    m_r = new SliderSpin(0, 255, 0); m_g = new SliderSpin(0, 255, 0); m_b = new SliderSpin(0, 255, 0);
    m_hex = new QLineEdit; m_hex->setMaxLength(7); m_hex->setPlaceholderText("#RRGGBB");
    auto row = [&](const QString& t, QWidget* w) { auto* h = new QHBoxLayout; auto* l = new QLabel(t); l->setFixedWidth(16); h->addWidget(l); h->addWidget(w, 1); v->addLayout(h); };
    v->addWidget(m_target);
    row("R", m_r); row("V", m_g); row("B", m_b); row("#", m_hex);
    v->addStretch();
    auto apply = [this] {
        if (m_busy) return;
        QColor c(int(m_r->value()), int(m_g->value()), int(m_b->value()));
        m_target->currentIndex() == 0 ? Workspace::instance().setFg(c) : Workspace::instance().setBg(c);
    };
    connect(m_r, &SliderSpin::valueChanged, this, apply); connect(m_g, &SliderSpin::valueChanged, this, apply); connect(m_b, &SliderSpin::valueChanged, this, apply);
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QColor c(m_hex->text());
        if (!c.isValid()) return;
        m_target->currentIndex() == 0 ? Workspace::instance().setFg(c) : Workspace::instance().setBg(c);
    });
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { sync(); });
    connect(&Workspace::instance(), &Workspace::colorsChanged, this, [this] { sync(); });
    sync();
}

void ColorPanel::sync() {
    m_busy = true;
    QColor c = m_target->currentIndex() == 0 ? Workspace::instance().fg : Workspace::instance().bg;
    m_r->setValue(c.red()); m_g->setValue(c.green()); m_b->setValue(c.blue());
    m_hex->setText(c.name().toUpper());
    m_busy = false;
}

// ============================================================================ Nuancier
SwatchesPanel::SwatchesPanel(QWidget* parent) : QWidget(parent) {
    for (int i = 0; i < 12; ++i) m_colors.push_back(QColor::fromRgb(i * 255 / 11, i * 255 / 11, i * 255 / 11));
    const double sv[][2] = {{1, 1}, {0.6, 1}, {0.3, 1}, {1, 0.75}, {1, 0.5}, {0.6, 0.5}};
    for (auto& p : sv) for (int h = 0; h < 12; ++h) m_colors.push_back(QColor::fromHsvF(h / 12.0, p[0], p[1]));
    setMinimumSize(sizeHint());
}
void SwatchesPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    for (size_t i = 0; i < m_colors.size(); ++i) {
        QRect r(4 + int(i % 12) * 20, 4 + int(i / 12) * 20, 18, 18);
        p.fillRect(r, m_colors[i]);
        p.setPen(QColor(20, 20, 20)); p.drawRect(r);
    }
}
void SwatchesPanel::mousePressEvent(QMouseEvent* e) {
    int c = (e->x() - 4) / 20, r = (e->y() - 4) / 20;
    size_t i = size_t(r * 12 + c);
    if (c < 0 || c >= 12 || r < 0 || i >= m_colors.size()) return;
    if (e->modifiers() & Qt::ControlModifier) Workspace::instance().setBg(m_colors[i]); else Workspace::instance().setFg(m_colors[i]);
}

// ============================================================================ Histogramme
HistogramPanel::HistogramPanel(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    m_channel = new QComboBox; m_channel->addItems({"RVB", "Rouge", "Vert", "Bleu", "Luminosité"});
    v->addWidget(m_channel);
    v->addStretch();
    setMinimumHeight(130);
    m_timer.setSingleShot(true); m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, [this] { compute(); update(); });
    connect(m_channel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { update(); });
}

void HistogramPanel::setDocument(Document* d) {
    if (m_doc) m_doc->disconnect(this);
    m_doc = d;
    if (d) connect(d, &Document::changed, this, [this] { m_timer.start(); });
    compute(); update();
}

void HistogramPanel::compute() {
    for (auto& h : m_h) h.assign(256, 0.f);
    if (!m_doc) return;
    cv::Mat src = m_doc->composite(), small;
    double s = std::min(1.0, 400.0 / std::max(src.cols, src.rows));
    cv::resize(src, small, cv::Size(), s, s, cv::INTER_AREA);
    for (int y = 0; y < small.rows; ++y)
        for (int x = 0; x < small.cols; ++x) {
            auto p = small.at<cv::Vec4b>(y, x);
            if (p[3] == 0) continue;
            m_h[0][p[2]]++; m_h[1][p[1]]++; m_h[2][p[0]]++;
            m_h[3][std::clamp(int(mu::lum(p[2], p[1], p[0])), 0, 255)]++;
        }
}

void HistogramPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QRect area(8, m_channel->height() + 12, width() - 16, height() - m_channel->height() - 20);
    p.fillRect(area, QColor(24, 24, 24));
    if (!m_doc) return;
    int ch = m_channel->currentIndex();
    auto draw = [&](const std::vector<float>& h, QColor c, float maxv) {
        p.setPen(Qt::NoPen);
        c.setAlpha(150); p.setBrush(c);
        QPolygonF poly; poly << QPointF(area.left(), area.bottom());
        for (int i = 0; i < 256; ++i) poly << QPointF(area.left() + i * area.width() / 255.0, area.bottom() - std::min(1.f, h[i] / maxv) * area.height());
        poly << QPointF(area.right(), area.bottom());
        p.drawPolygon(poly);
    };
    auto maxOf = [](const std::vector<float>& h) { float m = 1; for (int i = 1; i < 255; ++i) m = std::max(m, h[i]); return m * 1.05f; };
    if (ch == 0) {
        float m = std::max({maxOf(m_h[0]), maxOf(m_h[1]), maxOf(m_h[2])});
        draw(m_h[0], QColor(255, 60, 60), m); draw(m_h[1], QColor(60, 220, 60), m); draw(m_h[2], QColor(70, 110, 255), m);
    } else {
        static const QColor cols[] = {QColor(), QColor(255, 60, 60), QColor(60, 220, 60), QColor(70, 110, 255), QColor(220, 220, 220)};
        const auto& h = m_h[ch == 4 ? 3 : ch - 1];
        draw(h, cols[ch], maxOf(h));
    }
}
