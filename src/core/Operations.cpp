#include "Operations.h"
#include "MatUtil.h"
#include "Selection.h"
#include "Workspace.h"
#include "ai/AIBackend.h"
#include <opencv2/imgproc.hpp>
#include <QClipboard>
#include <QGuiApplication>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(Operations) }; }   // traductions hors classes QObject (voir translations/)

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
    d->applyStructure({size, {Layer::create(background == 2 ? Tr::tr("Calque 1") : Tr::tr("Arrière-plan"), mu::newMat(size, c))}, 0});
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
    auto l = Layer::createEmpty(name.isEmpty() ? uniqueLayerName(d, Tr::tr("Calque")) : name, d->size());
    insertAbove(d, l, Tr::tr("Nouveau calque"));
    return l;
}

void addLayerWithImage(Document* d, const cv::Mat& bgra, const QString& name) {
    insertAbove(d, Layer::create(uniqueLayerName(d, name), bgra), Tr::tr("Nouveau calque"));
}

void duplicateLayer(Document* d) {
    auto src = d->activeLayer();
    if (!src) return;
    auto l = src->clone();
    l->props.name = Tr::tr("%1 copie").arg(src->props.name);
    insertAbove(d, l, Tr::tr("Dupliquer le calque"));
}

void layerViaCopy(Document* d, bool cut) {
    auto l = d->activeLayer();
    if (!l) return;
    if (!d->hasSelection()) { if (!cut) duplicateLayer(d); return; }
    cv::Mat content = bakedImage(*l), a;
    cv::extractChannel(content, a, 3);
    cv::multiply(a, d->selection(), a, 1.0 / 255.0);
    cv::insertChannel(a, content, 3);
    d->undoStack()->beginMacro(cut ? Tr::tr("Calque par couper") : Tr::tr("Calque par copier"));
    if (cut) clearSelection(d);
    insertAbove(d, Layer::create(uniqueLayerName(d, Tr::tr("Calque")), content), Tr::tr("Nouveau calque"));
    d->undoStack()->endMacro();
}

void deleteLayer(Document* d) {
    if (d->layers().size() <= 1) { notify(Tr::tr("Impossible de supprimer le dernier calque.")); return; }
    int i = d->activeIndex();
    d->doStructural(Tr::tr("Supprimer le calque"), [&] {
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
    d->doStructural(Tr::tr("Fusionner vers le bas"), [&] {
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
    d->doStructural(Tr::tr("Fusionner les calques visibles"), [&] {
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
    auto nl = Layer::create(Tr::tr("Arrière-plan"), acc);
    d->doStructural(Tr::tr("Aplatir l'image"), [&] { d->mutableLayers() = {nl}; d->setActiveInternal(0); });
}

void reorderByIds(Document* d, const std::vector<int>& ids) {
    Document::LayerList nv;
    for (int id : ids) if (auto l = d->layerById(id)) nv.push_back(l);
    if (nv.size() != d->layers().size() || nv == d->layers()) return;
    auto active = d->activeLayer();
    d->doStructural(Tr::tr("Réorganiser les calques"), [&] {
        d->mutableLayers() = nv;
        d->setActiveInternal(int(std::find(nv.begin(), nv.end(), active) - nv.begin()));
    });
}

void moveLayer(Document* d, int delta) {
    int i = d->activeIndex(), j = i + delta;
    if (j < 0 || j >= int(d->layers().size())) return;
    d->doStructural(Tr::tr("Déplacer le calque"), [&] { std::swap(d->mutableLayers()[i], d->mutableLayers()[j]); d->setActiveInternal(j); });
}

void moveLayerToEnd(Document* d, bool top) {
    int i = d->activeIndex(), n = int(d->layers().size());
    int j = top ? n - 1 : 0;
    if (i == j) return;
    d->doStructural(Tr::tr("Déplacer le calque"), [&] {
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
    d->doLayerChange(Tr::tr("Ajouter un masque de fusion"), l, [&](Layer& L) { L.mask = m; L.props.maskEnabled = true; });
    l->editingMask = true;
    d->notifyLayerPixels(l.get());
}

void deleteMask(Document* d, bool apply) {
    auto l = d->activeLayer();
    if (!l || !l->hasMask()) return;
    d->doLayerChange(apply ? Tr::tr("Appliquer le masque") : Tr::tr("Supprimer le masque"), l, [&](Layer& L) {
        if (apply) L.image = bakedImage(L);
        L.mask = cv::Mat();
    });
    l->editingMask = false;
    d->notifyLayerPixels(l.get());
}

void flipLayer(Document* d, bool horizontal) {
    auto l = d->editableLayer();
    if (!l) return;
    d->doLayerChange(horizontal ? Tr::tr("Miroir horizontal du calque") : Tr::tr("Miroir vertical du calque"), l, [&](Layer& L) {
        cv::Mat o; cv::flip(L.image, o, horizontal ? 1 : 0); L.image = o;
        if (L.hasMask()) { cv::Mat m; cv::flip(L.mask, m, horizontal ? 1 : 0); L.mask = m; }
        L.text.reset();
    });
}

Layer::Ptr commitText(Document* d, const Layer::Ptr& existing, const TextData& td) {
    QString title = td.text.section('\n', 0, 0).left(24);
    if (existing && existing->isText()) {
        d->doLayerChange(Tr::tr("Modifier le texte"), existing, [&](Layer& L) {
            L.text = td; L.image = mu::newMat(d->size()); L.renderText(); L.props.name = title;
        });
        return existing;
    }
    auto l = Layer::createEmpty(title, d->size());
    l->text = td;
    l->renderText();
    insertAbove(d, l, Tr::tr("Texte"));
    return l;
}

void rasterizeText(Document* d) {
    auto l = d->activeLayer();
    if (l && l->isText()) d->doLayerChange(Tr::tr("Pixelliser le texte"), l, [](Layer& L) { L.text.reset(); });
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
    transformAll(d, Tr::tr("Taille de l'image"), ns, [&](const cv::Mat& m, bool isMask) {
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
    reframe(d, QRect(-dx, -dy, ns.width(), ns.height()), Tr::tr("Taille de la zone de travail"));
}

void cropTo(Document* d, const QRect& r) { reframe(d, r, Tr::tr("Recadrer")); }

void rotateImage(Document* d, int deg) {
    int code = deg == 90 ? cv::ROTATE_90_CLOCKWISE : deg == 180 ? cv::ROTATE_180 : cv::ROTATE_90_COUNTERCLOCKWISE;
    QSize ns = deg == 180 ? d->size() : QSize(d->size().height(), d->size().width());
    transformAll(d, Tr::tr("Rotation %1° horaire").arg(deg), ns, [&](const cv::Mat& m, bool) { cv::Mat o; cv::rotate(m, o, code); return o; });
}

void flipImage(Document* d, bool h) {
    transformAll(d, h ? Tr::tr("Miroir horizontal de l'image") : Tr::tr("Miroir vertical de l'image"), d->size(), [&](const cv::Mat& m, bool) { cv::Mat o; cv::flip(m, o, h ? 1 : 0); return o; });
}

// ------------------------------------------------------------------------------------------ sélection
void selectAll(Document* d) { d->setSelection(cv::Mat(d->size().height(), d->size().width(), CV_8UC1, cv::Scalar(255)), Tr::tr("Tout sélectionner")); }
void deselect(Document* d) { d->setSelection(cv::Mat(), Tr::tr("Désélectionner")); }
void invertSelection(Document* d) { d->setSelection(Sel::invert(d->selection(), d->size()), Tr::tr("Inverser la sélection")); }
void modifySelection(Document* d, int kind, double a) {
    if (!d->hasSelection()) return;
    const cv::Mat& s = d->selection();
    switch (kind) {
    case 0: d->setSelection(Sel::feather(s, a), Tr::tr("Contour progressif")); break;
    case 1: d->setSelection(Sel::grow(s, int(a)), Tr::tr("Agrandir la sélection")); break;
    case 2: d->setSelection(Sel::grow(s, -int(a)), Tr::tr("Contracter la sélection")); break;
    default: d->setSelection(Sel::smooth(s, int(a)), Tr::tr("Lisser la sélection")); break;
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
    d->doLayerChange(Tr::tr("Effacer"), l, [&](Layer& L) {
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
    auto l = Layer::create(uniqueLayerName(d, Tr::tr("Calque")), full);
    insertAbove(d, l, Tr::tr("Coller"));
    return l;
}

// ------------------------------------------------------------------------------------------ IA
bool removeBackground(Document* d, const cv::Mat& raw, const RemoveBgParams& p, QString* error, QString* warning) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (raw.empty() || raw.type() != CV_8UC1 || raw.size() != cv::Size(d->size().width(), d->size().height()))
        return fail(Tr::tr("Masque de détourage invalide (taille différente du document)."));
    cv::Mat refined = refineMask(raw, p.mask);

    if (p.output == RemoveBgParams::SelectionOnly) {
        d->setSelection(refined, Tr::tr("Sélection du sujet (IA)"));
        return true;
    }
    QString why;
    auto l = d->editableLayer(&why);
    if (!l) return fail(why);

    if (p.output == RemoveBgParams::LayerMask) {
        cv::Mat nm = refined;
        if (l->hasMask()) cv::multiply(l->mask, refined, nm, 1.0 / 255.0);   // combine avec un masque existant
        d->doLayerChange(Tr::tr("Supprimer l'arrière-plan (masque)"), l, [&](Layer& L) { L.mask = nm; L.props.maskEnabled = true; L.editingMask = false; });
        return true;
    }

    // NewLayer / ReplaceLayer : les pixels sont modifiés (couleurs de bord corrigées si demandé)
    const bool fromComposite = p.sampleAll && p.output == RemoveBgParams::NewLayer;
    cv::Mat source = fromComposite ? d->compositeCopy() : l->image;
    cv::Mat colored = source;
    if (p.defringe) {
        auto r = AIBackend::estimateForeground(source, refined, p.defringeRadius);
        if (r.ok) {
            cv::Mat rgb = r.data.clone();
            cv::Mat srcA;
            cv::extractChannel(source, srcA, 3);
            cv::insertChannel(srcA, rgb, 3);       // conserve l'alpha d'origine (le masque est appliqué ensuite)
            colored = rgb;
        } else if (warning) *warning = Tr::tr("Couleurs de bord non corrigées : %1").arg(r.error);
    }
    cv::Mat cut = applyMaskToAlpha(colored, refined);
    if (p.output == RemoveBgParams::NewLayer) {
        insertAbove(d, Layer::create(uniqueLayerName(d, Tr::tr("Sujet")), cut), Tr::tr("Supprimer l'arrière-plan (nouveau calque)"));
    } else {
        d->doLayerChange(Tr::tr("Supprimer l'arrière-plan"), l, [&](Layer& L) { L.image = cut; L.text.reset(); });
    }
    return true;
}

bool applyDepth(Document* d, const cv::Mat& rawDepth, const DepthApplyParams& p, QString* error) {
    if (rawDepth.empty() || rawDepth.type() != CV_8UC1 || rawDepth.size() != cv::Size(d->size().width(), d->size().height())) {
        if (error) *error = Tr::tr("Carte de profondeur invalide (taille différente du document).");
        return false;
    }
    if (!p.makeLayer && !p.makeSelection) { if (error) *error = Tr::tr("Rien à créer : cochez « Créer un calque » et/ou « Créer une sélection »."); return false; }
    cv::Mat depth = refineDepth(rawDepth, p.depth);
    d->undoStack()->beginMacro(Tr::tr("Carte de profondeur (IA)"));
    if (p.makeLayer) {
        cv::Mat bgra;
        cv::cvtColor(depth, bgra, cv::COLOR_GRAY2BGRA);
        cv::insertChannel(cv::Mat(depth.size(), CV_8UC1, cv::Scalar(255)), bgra, 3);
        insertAbove(d, Layer::create(uniqueLayerName(d, Tr::tr("Profondeur")), bgra), Tr::tr("Nouveau calque"));
    }
    if (p.makeSelection) d->setSelection(depthToSelection(depth, p.threshold, p.selectBright, p.feather), Tr::tr("Sélection par profondeur"));
    d->undoStack()->endMacro();
    return true;
}

bool aiUpscale(Document* d, const UpscaleParams& p, QString* error, const std::function<bool(int, int)>& progress) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    const int n = int(d->layers().size());
    const cv::Size old(d->size().width(), d->size().height());

    // Facteur du modèle : on le connaît après le 1er calque ; on borne la taille AVANT de lancer un calcul très long.
    std::vector<cv::Mat> images, masks(n);
    int scale = 0;
    cv::Size target;
    for (int i = 0; i < n; ++i) {
        if (progress && !progress(i, n)) return fail(Tr::tr("Opération annulée."));
        const auto& l = d->layers()[i];
        cv::Mat bled = bleedColors(l->image);
        auto r = AIBackend::upscale(bled);
        if (!r.ok) return fail(r.error);
        if (i == 0) {
            scale = r.scale;
            double fs = p.nativeScale ? double(scale) : p.finalScale;
            target = cv::Size(std::max(1, int(std::lround(old.width * fs))), std::max(1, int(std::lround(old.height * fs))));
            if (static_cast<long long>(target.width) * target.height > p.maxPixels)
                return fail(Tr::tr("Résultat trop grand (%1 × %2 px) : limite de sécurité %3 mégapixels. Réduisez l'image ou le facteur.")
                                .arg(target.width).arg(target.height).arg(p.maxPixels / 1000000));
        }
        if (r.data.size() != old * scale) return fail(Tr::tr("Taille de sortie inattendue du modèle."));
        cv::Mat up = r.data;                                   // RGB agrandi par le modèle (alpha de sortie ignoré)
        cv::Mat alpha8;
        cv::extractChannel(l->image, alpha8, 3);
        cv::Mat alphaUp;
        cv::resize(alpha8, alphaUp, up.size(), 0, 0, cv::INTER_CUBIC);
        cv::insertChannel(alphaUp, up, 3);
        if (up.size() != target) cv::resize(up, up, target, 0, 0, up.cols > target.width ? cv::INTER_AREA : cv::INTER_CUBIC);
        images.push_back(up);
        if (l->hasMask()) cv::resize(l->mask, masks[i], target, 0, 0, cv::INTER_LINEAR);
    }
    Document::LayerList nl;
    for (int i = 0; i < n; ++i) nl.push_back(shell(*d->layers()[i], images[i], masks[i]));
    QSize ns(target.width, target.height);
    d->doStructural(Tr::tr("Agrandissement IA ×%1").arg(double(target.width) / old.width, 0, 'g', 3), [&] { d->mutableLayers() = nl; d->setSizeInternal(ns); });
    return true;
}

// ------------------------------------------------------------------------------------------ Stable Diffusion
bool sdApplyInpaint(Document* d, const Sd::InpaintPlan& plan, const cv::Mat& generated, QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    QString why;
    auto l = d->editableLayer(&why);
    if (!l) return fail(why);
    if (generated.type() != CV_8UC4 || generated.size() != plan.work) return fail(Tr::tr("Image générée invalide (taille de travail attendue)."));
    if (plan.blendMask.size() != cv::Size(d->size().width(), d->size().height())) return fail(Tr::tr("Le document a changé de taille depuis la préparation de l'inpainting."));
    cv::Mat out = Sd::compositeInpaint(l->image, plan, generated);
    d->doLayerChange(Tr::tr("Inpainting (Stable Diffusion)"), l, [&](Layer& L) { L.image = out; L.text.reset(); });
    return true;
}

static QString sdLayerName(const QString& prompt) {
    QString p = prompt.simplified();
    if (p.size() > 28) p = p.left(27) + QStringLiteral("…");
    return p.isEmpty() ? Tr::tr("Image IA") : Tr::tr("IA : %1").arg(p);
}

Layer::Ptr sdAddImage(Document* d, const cv::Mat& generated, Sd::Placement placement, const QString& prompt, QString* error) {
    if (generated.type() != CV_8UC4 || generated.empty()) { if (error) *error = Tr::tr("Image générée invalide."); return nullptr; }
    cv::Mat img = Sd::placeGenerated(generated, d->size(), placement, d->selection());
    auto l = Layer::create(uniqueLayerName(d, sdLayerName(prompt)), img);
    insertAbove(d, l, Tr::tr("Génération d'image (Stable Diffusion)"));
    return l;
}

Document* sdNewDocument(const cv::Mat& generated, const QString& prompt) {
    QSize s(generated.cols, generated.rows);
    auto* d = new Document(s);
    d->applyStructure({s, {Layer::create(sdLayerName(prompt), generated.clone())}, 0});
    d->undoStack()->clear();
    return d;
}

void applyEffect(Document* d, const Effect& e, const Params& p) {
    QString why;
    auto l = d->editableLayer(&why);
    if (!l) { notify(why); return; }
    if (e.requiresSelection && !d->hasSelection()) { notify(Tr::tr("%1 : sélectionnez d'abord une zone.").arg(e.name.section(QStringLiteral("…"), 0, 0))); return; }
    cv::Mat source = (e.supportsSampleAllLayers && p.b("sampleAll")) ? d->compositeCopy() : l->image;
    EffectDiag::takeError();                                  // purge d'éventuelles erreurs anciennes
    cv::Mat res = e.run(source, d->selection(), p);
    if (QString err = EffectDiag::takeError(); !err.isEmpty()) { notify(err); return; }   // échec : aucun changement, aucune entrée d'historique
    d->doLayerChange(e.name.section(QStringLiteral("…"), 0, 0), l, [&](Layer& L) {
        L.image = blendWithSelection(L.image, res, d->selection());
        L.text.reset();
    });
}
}
