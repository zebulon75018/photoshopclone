#include "OptionsBar.h"
#include "CanvasView.h"
#include "core/Workspace.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>

OptionsBar::OptionsBar(QWidget* parent) : QToolBar(tr("Options"), parent) {
    setMovable(false);
    setObjectName("OptionsBar");
    setIconSize(QSize(16, 16));
}

void OptionsBar::clearOptions() {
    for (QAction* a : actions()) {
        if (QWidget* w = widgetForAction(a)) w->deleteLater();
        removeAction(a);
        delete a;
    }
}

void OptionsBar::addLabel(const QString& t) { auto* l = new QLabel(t); l->setStyleSheet("color:#bbb;padding:0 4px;"); addWidget(l); }

static void refocus() { if (auto* v = Workspace::instance().view()) v->setFocus(); }

void OptionsBar::addSpin(const QString& label, int* value, int mn, int mx, const QString& suffix) {
    addLabel(label);
    auto* s = new QSpinBox;
    s->setRange(mn, mx); s->setValue(*value); s->setSuffix(suffix); s->setKeyboardTracking(false);
    s->setFixedWidth(suffix.isEmpty() ? 70 : 84);
    connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [value](int v) { *value = v; Workspace::instance().settingsModified(); });
    connect(s, &QSpinBox::editingFinished, this, [] { refocus(); });
    addWidget(s);
}

void OptionsBar::addCheck(const QString& label, bool* value) {
    auto* c = new QCheckBox(label);
    c->setChecked(*value);
    connect(c, &QCheckBox::toggled, this, [value](bool v) { *value = v; Workspace::instance().settingsModified(); });
    addWidget(c);
}

void OptionsBar::addCombo(const QString& label, const QStringList& items, int* value) {
    addLabel(label);
    auto* c = new QComboBox;
    c->addItems(items); c->setCurrentIndex(std::clamp(*value, 0, items.size() - 1));
    connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [value](int i) { *value = i; Workspace::instance().settingsModified(); refocus(); });
    addWidget(c);
}

void OptionsBar::addSelectionModes(Sel::Mode* mode) {
    auto* g = new QButtonGroup(this);
    struct M { const char* txt; const char* tip; Sel::Mode m; };
    for (M m : {M{QT_TR_NOOP("Nouvelle"), QT_TR_NOOP("Nouvelle sélection"), Sel::Mode::Replace}, M{"+", QT_TR_NOOP("Ajouter à la sélection (Maj)"), Sel::Mode::Add},
                M{"−", QT_TR_NOOP("Soustraire de la sélection (Alt)"), Sel::Mode::Subtract}, M{"∩", QT_TR_NOOP("Intersection avec la sélection (Maj+Alt)"), Sel::Mode::Intersect}}) {
        auto* b = new QToolButton;
        b->setText(tr(m.txt)); b->setToolTip(tr(m.tip)); b->setCheckable(true); b->setChecked(*mode == m.m);
        b->setMinimumWidth(28);
        g->addButton(b);
        Sel::Mode mm = m.m;
        connect(b, &QToolButton::clicked, this, [mode, mm] { *mode = mm; Workspace::instance().settingsModified(); });
        addWidget(b);
    }
}

void OptionsBar::addFontControls() {
    auto& s = Workspace::instance().settings;
    auto* f = new QFontComboBox;
    f->setCurrentFont(QFont(s.fontFamily));
    connect(f, &QFontComboBox::currentFontChanged, this, [](const QFont& fnt) { Workspace::instance().settings.fontFamily = fnt.family(); });
    addWidget(f);
    addSpin(tr("Taille :"), &s.fontSize, 4, 1000, " px");
    auto mk = [&](const QString& t, const QString& tip, bool* v, int weight, bool italic) {
        auto* b = new QToolButton; b->setText(t); b->setToolTip(tip); b->setCheckable(true); b->setChecked(*v);
        QFont bf = b->font(); bf.setBold(weight); bf.setItalic(italic); b->setFont(bf);
        connect(b, &QToolButton::toggled, this, [v](bool on) { *v = on; });
        addWidget(b);
    };
    mk(tr("G", "initiale du bouton Gras"), tr("Gras"), &s.bold, 1, false);
    mk(tr("I", "initiale du bouton Italique"), tr("Italique"), &s.italic, 0, true);
    addCombo("", {tr("Gauche"), tr("Centré"), tr("Droite")}, &s.textAlign);
}

void OptionsBar::addButton(const QString& text, const std::function<void()>& fn) {
    auto* b = new QPushButton(text);
    connect(b, &QPushButton::clicked, this, [fn] { fn(); });
    addWidget(b);
}
