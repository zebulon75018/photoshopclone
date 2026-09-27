#include "MovableDialog.h"
#include "core/Workspace.h"
#include "ui/CanvasView.h"
#include <QApplication>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QEventLoop>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <vector>

namespace {
QPoint clampInside(const QWidget* host, QPoint p) {
    const QWidget* par = host->parentWidget();
    if (!par) return p;
    return QPoint(std::clamp(p.x(), 0, std::max(0, par->width() - host->width())), std::clamp(p.y(), 0, std::max(0, par->height() - host->height())));
}

// Barre de titre glissable (objectName "DialogTitleBar")
class TitleBar : public QWidget {
public:
    TitleBar(QWidget* host, const QString& title, std::function<void()> onClose) : QWidget(host), m_host(host) {
        setObjectName("DialogTitleBar");
        setFixedHeight(26);
        setStyleSheet("#DialogTitleBar{background:#1f1f1f;} QLabel{color:#e6e6e6;font-weight:bold;} QToolButton{border:0;color:#ccc;padding:0 8px;} QToolButton:hover{background:#c0392b;color:white;}");
        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(8, 0, 0, 0);
        auto* t = new QLabel(title);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* x = new QToolButton; x->setText("✕"); x->setToolTip("Fermer (Échap)");
        QObject::connect(x, &QToolButton::clicked, [onClose] { onClose(); });
        l->addWidget(t, 1); l->addWidget(x);
        setCursor(Qt::SizeAllCursor);
    }
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) { m_grab = e->globalPos() - m_host->mapToGlobal(QPoint(0, 0)); m_drag = true; m_host->raise(); }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_drag) return;
        QWidget* par = m_host->parentWidget();
        m_host->move(clampInside(m_host, par->mapFromGlobal(e->globalPos() - m_grab)));
    }
    void mouseReleaseEvent(QMouseEvent*) override { m_drag = false; }
private:
    QWidget* m_host; QPoint m_grab; bool m_drag = false;
};

class DialogHost : public QFrame {
public:
    DialogHost(QWidget* mw, const QString& title, std::function<void()> onClose) : QFrame(mw) {
        setObjectName("DialogHost");
        setFrameShape(QFrame::Box);
        setStyleSheet("QFrame#DialogHost{background:#323232;border:1px solid #7a7a7a;}");
        setAutoFillBackground(true);
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(1, 1, 1, 1); v->setSpacing(0);
        v->addWidget(new TitleBar(this, title, std::move(onClose)));
        mw->installEventFilter(this);
        hide();
    }
    void setContent(QWidget* w) { static_cast<QVBoxLayout*>(layout())->addWidget(w); layout()->activate(); resize(sizeHint().expandedTo(minimumSizeHint())); }
    void placeInitially(const QString& key) {
        QWidget* mw = parentWidget();
        QSettings s("PhotoClone", "PhotoClone");
        QPoint p;
        if (s.contains("dialogs/" + key)) p = s.value("dialogs/" + key).toPoint();
        else {   // défaut : coin supérieur droit de la zone centrale, pour laisser l'image visible
            QWidget* c = qobject_cast<QMainWindow*>(mw) ? qobject_cast<QMainWindow*>(mw)->centralWidget() : nullptr;
            QRect cr = c ? QRect(c->mapTo(mw, QPoint(0, 0)), c->size()) : mw->rect();
            p = QPoint(cr.right() - width() - 24, cr.top() + 24);
        }
        move(clampInside(this, p));
    }
    void savePosition(const QString& key) { QSettings("PhotoClone", "PhotoClone").setValue("dialogs/" + key, pos()); }
protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize) move(clampInside(this, pos()));   // reste dans la fenêtre
        return QFrame::eventFilter(o, e);
    }
};
}

int execMovable(QDialog* dlg, const QString& key) {
    QWidget* mw = Workspace::instance().mainWindow();
    if (!mw || !mw->isVisible()) return dlg->QDialog::exec();          // repli : dialogue système classique

    QWidget* oldParent = dlg->parentWidget();
    const Qt::WindowFlags oldFlags = dlg->windowFlags();
    QEventLoop loop;
    DialogHost host(mw, dlg->windowTitle(), [dlg] { dlg->reject(); });
    dlg->setParent(&host, Qt::Widget);
    host.setContent(dlg);
    host.placeInitially(key);

    // inhibe le reste de l'interface (mais pas le canevas : pan/zoom restent utilisables)
    struct Saved { QPointer<QWidget> w; bool wasEnabled; };
    std::vector<Saved> saved;
    QWidget* central = qobject_cast<QMainWindow*>(mw) ? qobject_cast<QMainWindow*>(mw)->centralWidget() : nullptr;
    for (QObject* o : mw->children()) {
        auto* w = qobject_cast<QWidget*>(o);
        if (!w || w == &host || w == central) continue;
        saved.push_back({w, w->isEnabled()}); w->setEnabled(false);
    }
    if (central) for (QTabBar* tb : central->findChildren<QTabBar*>()) { saved.push_back({tb, tb->isEnabled()}); tb->setEnabled(false); }
    Workspace::instance().pushModal();

    QObject::connect(dlg, &QDialog::finished, &loop, &QEventLoop::quit);
    host.show(); host.raise();
    dlg->show();
    dlg->setFocus();
    loop.exec();

    host.savePosition(key);
    Workspace::instance().popModal();
    for (auto& s : saved) if (s.w) s.w->setEnabled(s.wasEnabled);
    const int result = dlg->result();
    dlg->hide();
    dlg->setParent(oldParent, oldFlags);        // rend la propriété à l'appelant avant la destruction de l'hôte
    if (auto* v = Workspace::instance().view()) v->setFocus();
    return result;
}

int MovableDialog::exec() { return execMovable(this, positionKey()); }

void frenchButtons(QDialogButtonBox* bb) {
    struct B { QDialogButtonBox::StandardButton b; const char* t; };
    for (B x : {B{QDialogButtonBox::Ok, "OK"}, B{QDialogButtonBox::Cancel, "Annuler"}, B{QDialogButtonBox::Reset, "Réinitialiser"}, B{QDialogButtonBox::Close, "Fermer"}})
        if (auto* b = bb->button(x.b)) b->setText(QString::fromUtf8(x.t));
}

namespace Dlg {
QColor pickColor(const QColor& initial, const QString& title, QWidget* parent) {
    QColorDialog d(initial, parent);
    d.setWindowTitle(title);
    d.setOption(QColorDialog::DontUseNativeDialog);
    return execMovable(&d, "ColorDialog") == QDialog::Accepted ? d.currentColor() : QColor();
}

static MovableDialog* makePrompt(QWidget* parent, const QString& title, const QString& label, QWidget* editor, QFormLayout*& f) {
    auto* d = new MovableDialog(parent);
    d->setWindowTitle(title); d->setPositionKey("Prompt/" + title);
    f = new QFormLayout(d);
    f->addRow(label, editor);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, d, &QDialog::reject);
    frenchButtons(bb);
    f->addRow(bb);
    return d;
}

int getInt(QWidget* parent, const QString& title, const QString& label, int def, int mn, int mx, bool* ok) {
    auto* s = new QSpinBox; s->setRange(mn, mx); s->setValue(def);
    QFormLayout* f;
    std::unique_ptr<MovableDialog> d(makePrompt(parent, title, label, s, f));
    s->selectAll();
    bool accepted = d->exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? s->value() : def;
}

QString getText(QWidget* parent, const QString& title, const QString& label, const QString& def, bool* ok) {
    auto* e = new QLineEdit(def); e->setMinimumWidth(240);
    QFormLayout* f;
    std::unique_ptr<MovableDialog> d(makePrompt(parent, title, label, e, f));
    e->selectAll();
    bool accepted = d->exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? e->text() : def;
}
}
