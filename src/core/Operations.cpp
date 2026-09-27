#include "Operations.h"
#include "MatUtil.h"
#include "Selection.h"
#include "Workspace.h"
#include <opencv2/imgproc.hpp>
#include <QClipboard>
#include <QGuiApplication>

namespace Ops {
static cv::Mat bakedImage(const Layer& l);
void clearSelection(Document*);
static QPoint s_lastCopyPos(-1, -1);
static QSize s_lastCopyDoc;

static void notify(const QString& m) { Workspace::instance().message(m); }

QString uniqueLayerName(const Document* d, const QString& base) {
    for (int n = int(d->layers().size()) + 1;; ++n) {
        QString name = QString("%1 %2").arg(base).arg(n);
        bool used = false;
        for (auto& l : d->layers()) used |= l->props.name == name;
        if (!used) return name;
    }
}

Document* makeDocument(QSize size, int background, const QColor& bg) {
    auto* d = new Document(size);
    cv::Vec4b c = background == 0 ? cv::Vec4b(255, 255, 255, 255) : background == 1 ? mu::bgra(bg) : cv::Vec4b(0, 0, 0, 0);
    d->applyStructure({size, {Layer::create(background == 2 ? "Calque 1" : "Arrière-plan", mu::newMat(size, c))}, 0});
    d->undoStack()->clear();
    return d;
}

// ------------------------------------------------------------------------------------------ calques
static void insertAbove(Document* d, const Layer::Ptr& l, const QString& undo) {
    d->doStructural(undo, [&] {
        auto& v = d->mutableLayers();
        int at = v.empty() ? 0 : d->activeIndex() + 1;
        v.insert(v.begin() + at, l);
        d->setActiveInternal(at);
    });
}

Layer::Ptr addLayer(Document* d, const QString& name) {
    auto l = Layer::createEmpty(name.isEmpty() ? uniqueLayerName(d, "Calque") : name, d->size());
    insertAbove(d, l, "Nouveau calque");
    return l;
}

void addLayerWithImage(Document* d, const cv::Mat& bgra, const QString& name) {
    insertAbove(d, Layer::create(uniqueLayerName(d, name), bgra), "Nouveau calque");
}

void duplicateLayer(Document* d) {
    auto src = d->activeLayer();
    if (!src) return;
    auto l = src->clone();
    l->props.name = src->props.name + " copie";
    insertAbove(d, l, "Dupliquer le calque");
}

void layerViaCopy(Document* d, bool cut) {
    auto l = d->activeLayer();
    if (!l) return;
    if (!d->hasSelection()) { if (!cut) duplicateLayer(d); return; }
    cv::Mat content = bakedImage(*l), a;
    cv::extractChannel(content, a, 3);
    cv::multiply(a, d->selection(), a, 1.0 / 255.0);
    cv::insertChannel(a, content, 3);
    d->undoStack()->beginMacro(cut ? "Calque par couper" : "Calque par copier");
    if (cut) clearSelection(d);
    insertAbove(d, Layer::create(uniqueLayerName(d, "Calque"), content), "Nouveau calque");
    d->undoStack()->endMacro();
}

void deleteLayer(Document* d) {
    if (d->layers().size() <= 1) { notify("Impossible de supprimer le dernier calque."); return; }
    int i = d->activeIndex();
    d->doStructural("Supprimer le calque", [&] {
        auto& v = d->mutableLayers();
        v.erase(v.begin() + i);
        d->setActiveInternal(std::min(i, int(v.size()) - 1));
    });
}

static cv::Mat bakedImage(const Layer& l) {
    cv::Mat img = l.image.clone();
    if (l.hasMask() && l.props.maskEnabled) {
        cv::Mat a; cv::extractChannel(img, a, 3);
        cv::multiply(a, l.mask, a, 1.0 / 255.0);
        cv::insertChannel(a, img, 3);
    }
    return img;
}

void mergeDown(Document* d) {
    int i = d->activeIndex();
    if (i <= 0) return;
    auto up = d->layers()[i], lo = d->layers()[i - 1];
    cv::Mat base = bakedImage(*lo);
    if (up->props.visible)
        Blend::over(base, up->image, {0, 0}, mu::bounds(base), up->props.blend, up->props.opacity, up->props.maskEnabled ? up->mask : cv::Mat());
    auto nl = Layer::create(lo->props.name, base);
    nl->props = lo->props;
    d->doStructural("Fusionner vers le bas", [&] {
        auto& v = d->mutableLayers();
        v[i - 1] = nl;
        v.erase(v.begin() + i);
        d->setActiveInternal(i - 1);
    });
}

void mergeVisible(Document* d) {
    int first = -1;
    cv::Mat acc = mu::newMat(d->size());
    for (size_t i = 0; i < d->layers().size(); ++i) {
        auto& l = d->layers()[i];
        if (!l->props.visible) continue;
        if (first < 0) first = int(i);
        Blend::over(acc, l->image, {0, 0}, mu::bounds(acc), l->props.blend, l->props.opacity, l->props.maskEnabled ? l->mask : cv::Mat());
    }
    if (first < 0) return;
    auto nl = Layer::create(d->layers()[first]->props.name, acc);
    d->doStructural("Fusionner les calques visibles", [&] {
        auto& v = d->mutableLayers();
        Document::LayerList keep;
        for (size_t i = 0; i < v.size(); ++i) {
            if (int(i) == first) keep.push_back(nl);
            else if (!v[i]->props.visible) keep.push_back(v[i]);
        }
        v = keep;
        d->setActiveInternal(int(std::find(v.begin(), v.end(), nl) - v.begin()));
    });
}

void flatten(Document* d) {
    if (d->layers().size() <= 1) return;
    cv::Mat acc = mu::newMat(d->size(), {255, 255, 255, 255});
    for (auto& l : d->layers())
        if (l->props.visible) Blend::over(acc, l->image, {0, 0}, mu::bounds(acc), l->props.blend, l->props.opacity, l->props.maskEnabled ? l->mask : cv::Mat());
    auto nl = Layer::create("Arrière-plan", acc);
    d->doStructural("Aplatir l'image", [&] { d->mutableLayers() = {nl}; d->setActiveInternal(0); });
}

void reorderByIds(Document* d, const std::vector<int>& ids) {
    Document::LayerList nv;
    for (int id : ids) if (auto l = d->layerById(id)) nv.push_back(l);
    if (nv.size() != d->layers().size() || nv == d->layers()) return;
    auto active = d->activeLayer();
    d->doStructural("Réorganiser les calques", [&] {
        d->mutableLayers() = nv;
        d->setActiveInternal(int(std::find(nv.begin(), nv.end(), active) - nv.begin()));
    });
}

void moveLayer(Document* d, int delta) {
    int i = d->activeIndex(), j = i + delta;
    if (j < 0 || j >= int(d->layers().size())) return;
    d->doStructural("Déplacer le calque", [&] { std::swap(d->mutableLayers()[i], d->mutableLayers()[j]); d->setActiveInternal(j); });
}

void moveLayerToEnd(Document* d, bool top) {
    int i = d->activeIndex(), n = int(d->layers().size());
    int j = top ? n - 1 : 0;
    if (i == j) return;
    d->doStructural("Déplacer le calque", [&] {
        auto& v = d->mutableLayers(); auto l = v[i];
        v.erase(v.begin() + i); v.insert(v.begin() + j, l);
        d->setActiveInternal(j);
    });
}

void setProps(Document* d, const Layer::Ptr& l, const LayerProps& p, const QString& name) {
    LayerState before = l->state();
    l->props = p;
    d->pushLayerChange(name, l, before);
}

void addMask(Document* d, bool hideAll) {
    auto l = d->activeLayer();
    if (!l || l->hasMask()) return;
    cv::Mat m;
    if (d->hasSelection()) { m = d->selection().clone(); if (hideAll) cv::bitwise_not(m, m); }
    else m = cv::Mat(d->size().height(), d->size().width(), CV_8UC1, cv::Scalar(hideAll ? 0 : 255));
    d->doLayerChange("Ajouter un masque de fusion", l, [&](Layer& L) { L.mask = m; L.props.maskEnabled = true; });
    l->editingMask = true;
    d->notifyLayerPixels(l.get());
}

void deleteMask(Document* d, bool apply) {
    auto l = d->activeLayer();
    if (!l || !l->hasMask()) return;
    d->doLayerChange(apply ? "Appliquer le masque" : "Supprimer le masque", l, [&](Layer& L) {
        if (apply) L.image = bakedImage(L);
        L.mask = cv::Mat();
    });
    l->editingMask = false;
    d->notifyLayerPixels(l.get());
}

void flipLayer(Document* d, bool horizontal) {
    auto l = d->editableLayer();
    if (!l) return;
    d->doLayerChange(horizontal ? "Miroir horizontal du calque" : "Miroir vertical du calque", l, [&](Layer& L) {
        cv::Mat o; cv::flip(L.image, o, horizontal ? 1 : 0); L.image = o;
        if (L.hasMask()) { cv::Mat m; cv::flip(L.mask, m, horizontal ? 1 : 0); L.mask = m; }
        L.text.reset();
    });
}

Layer::Ptr commitText(Document* d, const Layer::Ptr& existing, const TextData& td) {
    QString title = td.text.section('\n', 0, 0).left(24);
    if (existing && existing->isText()) {
        d->doLayerChange("Modifier le texte", existing, [&](Layer& L) {
            L.text = td; L.image = mu::newMat(d->size()); L.renderText(); L.props.name = title;
        });
        return existing;
    }
    auto l = Layer::createEmpty(title, d->size());
    l->text = td;
    l->renderText();
    insertAbove(d, l, "Texte");
    return l;
}

void rasterizeText(Document* d) {
    auto l = d->activeLayer();
    if (l && l->isText()) d->doLayerChange("Pixelliser le texte", l, [](Layer& L) { L.text.reset(); });
}

// ------------------------------------------------------------------------------------------ image
static Layer::Ptr shell(const Layer& l, const cv::Mat& img, const cv::Mat& mask) {
    auto n = Layer::create(l.props.name, img);
    n->props = l.props;
    n->mask = mask;
    return n;
}

static void transformAll(Document* d, const QString& name, QSize newSize, const std::function<cv::Mat(const cv::Mat&, bool isMask)>& fn) {
    Document::LayerList nl;
    for (auto& l : d->layers()) nl.push_back(shell(*l, fn(l->image, false), l->hasMask() ? fn(l->mask, true) : cv::Mat()));
    d->doStructural(name, [&] { d->mutableLayers() = nl; d->setSizeInternal(newSize); });
}

void resizeImage(Document* d, QSize ns, int interp) {
    if (ns.isEmpty() || ns == d->size()) return;
    transformAll(d, "Taille de l'image", ns, [&](const cv::Mat& m, bool isMask) {
        cv::Mat o, pm = m.clone();
        bool shrink = ns.width() < d->size().width();
        int ip = shrink ? cv::INTER_AREA : interp;
        if (!isMask) mu::premultiply(pm);
        cv::resize(pm, o, cv::Size(ns.width(), ns.height()), 0, 0, isMask ? cv::INTER_LINEAR : ip);
        if (!isMask) mu::unpremultiply(o);
        return o;
    });
}

static void reframe(Document* d, const QRect& r, const QString& name) {
    if (r.isEmpty()) return;
    Document::LayerList nl;
    for (auto& l : d->layers()) {
        auto cut = [&](const cv::Mat& m, int type) {
            cv::Mat o = cv::Mat::zeros(r.height(), r.width(), type);
            cv::Rect src = mu::toCv(r) & mu::bounds(m);
            if (!src.empty()) m(src).copyTo(o(cv::Rect(src.x - r.x(), src.y - r.y(), src.width, src.height)));
            return o;
        };
        auto n = shell(*l, cut(l->image, CV_8UC4), l->hasMask() ? cut(l->mask, CV_8UC1) : cv::Mat());
        if (l->isText()) { n->text = l->text; n->text->pos -= r.topLeft(); n->renderText(); }
        nl.push_back(n);
    }
    d->doStructural(name, [&] { d->mutableLayers() = nl; d->setSizeInternal(r.size()); });
}

void resizeCanvas(Document* d, QSize ns, int anchor) {
    if (ns.isEmpty() || ns == d->size()) return;
    int col = anchor % 3, row = anchor / 3;
    int dx = col == 0 ? 0 : col == 1 ? (ns.width() - d->size().width()) / 2 : ns.width() - d->size().width();
    int dy = row == 0 ? 0 : row == 1 ? (ns.height() - d->size().height()) / 2 : ns.height() - d->size().height();
    reframe(d, QRect(-dx, -dy, ns.width(), ns.height()), "Taille de la zone de travail");
}

void cropTo(Document* d, const QRect& r) { reframe(d, r, "Recadrer"); }

void rotateImage(Document* d, int deg) {
    int code = deg == 90 ? cv::ROTATE_90_CLOCKWISE : deg == 180 ? cv::ROTATE_180 : cv::ROTATE_90_COUNTERCLOCKWISE;
    QSize ns = deg == 180 ? d->size() : QSize(d->size().height(), d->size().width());
    transformAll(d, QString("Rotation %1° horaire").arg(deg), ns, [&](const cv::Mat& m, bool) { cv::Mat o; cv::rotate(m, o, code); return o; });
}

void flipImage(Document* d, bool h) {
    transformAll(d, h ? "Miroir horizontal de l'image" : "Miroir vertical de l'image", d->size(), [&](const cv::Mat& m, bool) { cv::Mat o; cv::flip(m, o, h ? 1 : 0); return o; });
}

// ------------------------------------------------------------------------------------------ sélection
void selectAll(Document* d) { d->setSelection(cv::Mat(d->size().height(), d->size().width(), CV_8UC1, cv::Scalar(255)), "Tout sélectionner"); }
void deselect(Document* d) { d->setSelection(cv::Mat(), "Désélectionner"); }
void invertSelection(Document* d) { d->setSelection(Sel::invert(d->selection(), d->size()), "Inverser la sélection"); }
void modifySelection(Document* d, int kind, double a) {
    if (!d->hasSelection()) return;
    const cv::Mat& s = d->selection();
    switch (kind) {
    case 0: d->setSelection(Sel::feather(s, a), "Contour progressif"); break;
    case 1: d->setSelection(Sel::grow(s, int(a)), "Agrandir la sélection"); break;
    case 2: d->setSelection(Sel::grow(s, -int(a)), "Contracter la sélection"); break;
    default: d->setSelection(Sel::smooth(s, int(a)), "Lisser la sélection"); break;
    }
}

// ------------------------------------------------------------------------------------------ édition
void fillMask(Document* d, const cv::Mat& mask, const QColor& c, double opacity, const QString& name) {
    QString why;
    auto l = d->editableLayer(&why);
    if (!l) { notify(why); return; }
    cv::Mat sel = mask;
    if (d->hasSelection()) { cv::Mat s2; cv::multiply(mask, d->selection(), s2, 1.0 / 255.0); sel = s2; }
    cv::Rect r = Sel::bounds(sel);
    if (r.empty()) return;
    d->doLayerChange(name, l, [&](Layer& L) {
        cv::Mat out = L.image.clone();
        Blend::over(out, mu::newMat(d->size(), mu::bgra(c)), {0, 0}, r, BlendMode::Normal, float(opacity), cv::Mat(), sel);
        L.image = out;
        L.text.reset();
    });
}

void fillSelection(Document* d, const QColor& c, const QString& name) {
    fillMask(d, cv::Mat(d->size().height(), d->size().width(), CV_8UC1, cv::Scalar(255)), c, 1.0, name);
}

void clearSelection(Document* d) {
    QString why;
    auto l = d->editableLayer(&why);
    if (!l) { notify(why); return; }
    d->doLayerChange("Effacer", l, [&](Layer& L) {
        cv::Mat out = L.image.clone(), a;
        cv::extractChannel(out, a, 3);
        if (d->hasSelection()) { cv::Mat inv; cv::bitwise_not(d->selection(), inv); cv::multiply(a, inv, a, 1.0 / 255.0); }
        else a.setTo(0);
        cv::insertChannel(a, out, 3);
        L.image = out; L.text.reset();
    });
}

void copy(Document* d, bool merged) {
    auto l = d->activeLayer();
    if (!l) return;
    cv::Mat src = merged ? d->compositeCopy() : bakedImage(*l);
    cv::Rect bb = d->hasSelection() ? Sel::bounds(d->selection()) : mu::bounds(src);
    if (bb.empty()) return;
    cv::Mat out = src(bb).clone();
    if (d->hasSelection()) {
        cv::Mat a; cv::extractChannel(out, a, 3);
        cv::multiply(a, d->selection()(bb), a, 1.0 / 255.0);
        cv::insertChannel(a, out, 3);
    }
    QGuiApplication::clipboard()->setImage(mu::toQImage(out));
    s_lastCopyPos = QPoint(bb.x, bb.y);
    s_lastCopyDoc = d->size();
}

void cut(Document* d) { copy(d, false); clearSelection(d); }

Layer::Ptr paste(Document* d, bool inPlace, QPoint center) {
    QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) return nullptr;
    cv::Mat content = mu::fromQImage(img);
    QPoint pos = (inPlace && s_lastCopyDoc == d->size()) ? s_lastCopyPos : center - QPoint(content.cols / 2, content.rows / 2);
    cv::Mat full = mu::newMat(d->size());
    Blend::over(full, content, {pos.x(), pos.y()}, mu::bounds(full));
    auto l = Layer::create(uniqueLayerName(d, "Calque"), full);
    insertAbove(d, l, "Coller");
    return l;
}

void applyEffect(Document* d, const Effect& e, const Params& p) {
    QString why;
    auto l = d->editableLayer(&why);
    if (!l) { notify(why); return; }
    d->doLayerChange(e.name.section(QStringLiteral("…"), 0, 0), l, [&](Layer& L) {
        cv::Mat res = e.run(L.image, p);
        L.image = blendWithSelection(L.image, res, d->selection());
        L.text.reset();
    });
}
}
