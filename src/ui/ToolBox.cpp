#include "ToolBox.h"
#include "Icons.h"
#include "core/Workspace.h"
#include "tools/ToolManager.h"
#include "MovableDialog.h"
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

ColorSwatchWidget::ColorSwatchWidget(QWidget* parent) : QWidget(parent) {
    setFixedSize(46, 50);
    setToolTip(tr("Couleurs premier plan / arrière-plan\nX : permuter • D : par défaut"));
    connect(&Workspace::instance(), &Workspace::colorsChanged, this, [this] { update(); });
}

static const QRect kFg(3, 3, 26, 26), kBg(15, 15, 26, 26), kSwap(32, 0, 14, 14), kDef(2, 34, 14, 14);

void ColorSwatchWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    auto& w = Workspace::instance();
    p.setPen(QColor(10, 10, 10));
    p.setBrush(w.bg); p.drawRect(kBg);
    p.setBrush(w.fg); p.drawRect(kFg);
    p.setPen(QColor(220, 220, 220)); p.setBrush(Qt::NoBrush);
    p.drawText(kSwap, Qt::AlignCenter, "⇄");
    p.setBrush(Qt::white); p.drawRect(QRect(kDef.left() + 4, kDef.top() + 4, 8, 8));
    p.setBrush(Qt::black); p.drawRect(QRect(kDef.left(), kDef.top(), 8, 8));
}

void ColorSwatchWidget::mousePressEvent(QMouseEvent* e) {
    auto& w = Workspace::instance();
    if (kSwap.contains(e->pos())) { w.swapColors(); return; }
    if (kDef.contains(e->pos())) { w.resetColors(); return; }
    if (kFg.contains(e->pos())) { QColor c = Dlg::pickColor(w.fg, tr("Couleur de premier plan"), this); if (c.isValid()) w.setFg(c); }
    else if (kBg.contains(e->pos())) { QColor c = Dlg::pickColor(w.bg, tr("Couleur d'arrière-plan"), this); if (c.isValid()) w.setBg(c); }
}

ToolBox::ToolBox(ToolManager* tm, QWidget* parent) : QToolBar(tr("Outils"), parent), m_tm(tm) {
    setObjectName("ToolBox");
    setOrientation(Qt::Vertical);
    setMovable(false);
    setContentsMargins(2, 2, 2, 2);
    const auto& groups = tm->groups();
    for (int g = 0; g < groups.size(); ++g) {
        auto* b = new QToolButton;
        b->setCheckable(true); b->setAutoRaise(true); b->setFixedSize(40, 34); b->setIconSize(QSize(24, 24));
        if (groups[g].tools.size() > 1) {
            auto* menu = new QMenu(b);
            for (Tool* t : groups[g].tools) menu->addAction(toolIcon(t->id()), t->name(), this, [this, t] { m_tm->select(t); refresh(); });
            b->setMenu(menu);
            b->setPopupMode(QToolButton::DelayedPopup);
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QToolButton::customContextMenuRequested, this, [b, menu] { menu->exec(b->mapToGlobal(QPoint(b->width(), 0))); });
        }
        connect(b, &QToolButton::clicked, this, [this, g] { m_tm->selectGroup(g); refresh(); });
        addWidget(b);
        m_buttons.push_back(b);
    }
    addSeparator();
    addWidget(new ColorSwatchWidget);
    connect(tm, &ToolManager::toolChanged, this, [this] { refresh(); });
    connect(tm, &ToolManager::groupsChanged, this, [this] { refresh(); });
    refresh();
}

void ToolBox::refresh() {
    const auto& groups = m_tm->groups();
    for (int g = 0; g < groups.size(); ++g) {
        Tool* a = groups[g].active();
        m_buttons[g]->setIcon(toolIcon(a->id()));
        m_buttons[g]->setChecked(groups[g].tools.contains(m_tm->current()));
        m_buttons[g]->setToolTip(tr("%1\n(Maj+%2 : outil suivant du groupe)").arg(a->name(), groups[g].key));
    }
}
