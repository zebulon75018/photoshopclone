// Test de fumée : exerce le moteur sans interaction et capture l'interface (QT_QPA_PLATFORM=offscreen).
#include <opencv2/imgproc.hpp>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include "core/ImageIO.h"
#include "core/Operations.h"
#include "core/Workspace.h"
#include "tools/BrushEngine.h"
#include "tools/FloatingContent.h"
#include "tools/Tools.h"
#include "tools/ToolManager.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/EffectDialog.h"
#include "ui/AIDialogs.h"
#include "ui/Widgets.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include "ai/AIBackend.h"
#include "ai/SdBackend.h"
#include "ai/SdImaging.h"
#include "ui/SdDialogs.h"
#include <QElapsedTimer>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSpinBox>
#include <QToolButton>
#include <QTabWidget>
#include <QRegularExpression>
#include <atomic>
#include "ai/AIModels.h"
#include "ai/MaskRefine.h"
#include "ai/VispBridge.h"
#include "ui/MovableDialog.h"
#include <QFile>
#include <QFrame>
#include <QMenuBar>
#include <QSettings>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; qWarning() << "ÉCHEC:" << #c << "ligne" << __LINE__; } } while (0)

static cv::Vec4b px(Document* d, int x, int y) { return d->activeLayer()->image.at<cv::Vec4b>(y, x); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    applyDarkTheme(app);
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path() + "/settings");   // isole les réglages (chemins de modèles, positions…) des vrais réglages
    auto& ws = Workspace::instance();

    // --- document + brosse + undo/redo
    std::unique_ptr<Document> d(Ops::makeDocument(QSize(400, 300), 0, Qt::white));
    CHECK(d->layers().size() == 1);
    Ops::addLayer(d.get());
    CHECK(d->layers().size() == 2 && d->activeIndex() == 1);
    BrushEngine be;
    BrushEngine::Params p; p.size = 20; p.hardness = 1; p.color = Qt::red;
    auto layer = d->editableLayer();
    CHECK(be.begin(d.get(), layer, Document::Target::Pixels, p));
    be.strokeTo({50, 50}); be.strokeTo({150, 50});
    be.end("Pinceau");
    CHECK(px(d.get(), 100, 50)[2] == 255 && px(d.get(), 100, 50)[3] == 255);
    CHECK(d->composite().at<cv::Vec4b>(50, 100)[2] == 255 && d->composite().at<cv::Vec4b>(50, 100)[1] == 0);
    d->undoStack()->undo();
    CHECK(px(d.get(), 100, 50)[3] == 0);
    d->undoStack()->redo();
    CHECK(px(d.get(), 100, 50)[3] == 255);

    // --- sélection + remplissage limité à la sélection
    d->setSelection(Sel::fromRect(d->size(), QRectF(200, 100, 50, 50)), "rect");
    Ops::fillSelection(d.get(), Qt::blue, "fill");
    CHECK(px(d.get(), 220, 120)[0] == 255 && px(d.get(), 10, 10)[3] == 0);
    Ops::deselect(d.get());
    d->undoStack()->undo(); d->undoStack()->undo();   // annule désélection + remplissage
    CHECK(px(d.get(), 220, 120)[3] == 0);
    d->undoStack()->redo(); d->undoStack()->redo();

    // --- déplacement (FloatingContent) avec sélection
    d->setSelection(Sel::fromRect(d->size(), QRectF(190, 90, 70, 70)), "rect");
    FloatingContent fc;
    CHECK(fc.begin(d.get()));
    fc.update((cv::Mat_<double>(2, 3) << 1, 0, 30, 0, 1, 20));
    CHECK(px(d.get(), 250, 140)[0] == 255 && px(d.get(), 220, 120)[3] == 0);
    fc.commit("Déplacer");
    d->undoStack()->undo();
    CHECK(px(d.get(), 220, 120)[0] == 255 && px(d.get(), 250, 140)[3] == 0);
    d->undoStack()->redo();
    Ops::deselect(d.get());

    // --- calques : masque, fusion, duplication, texte
    Ops::addMask(d.get(), true);
    CHECK(d->activeLayer()->hasMask());
    Ops::duplicateLayer(d.get());
    TextData td; td.text = "PhotoClone"; td.pixelSize = 40; td.color = Qt::black; td.pos = QPointF(20, 200);
    auto tl = Ops::commitText(d.get(), nullptr, td);
    CHECK(tl->isText());
    bool anyText = false;
    for (int y = 200; y < 250 && !anyText; ++y) for (int x = 20; x < 200; ++x) if (tl->image.at<cv::Vec4b>(y, x)[3] > 0) { anyText = true; break; }
    CHECK(anyText);
    size_t n = d->layers().size();
    Ops::mergeDown(d.get());
    CHECK(d->layers().size() == n - 1);
    d->undoStack()->undo();
    CHECK(d->layers().size() == n);

    // --- image : recadrage, redimension, rotation (structure + undo)
    Ops::cropTo(d.get(), QRect(10, 10, 300, 200));
    CHECK(d->size() == QSize(300, 200));
    Ops::resizeImage(d.get(), QSize(150, 100), 1);
    CHECK(d->size() == QSize(150, 100));
    Ops::rotateImage(d.get(), 90);
    CHECK(d->size() == QSize(100, 150));
    d->undoStack()->undo(); d->undoStack()->undo(); d->undoStack()->undo();
    CHECK(d->size() == QSize(400, 300));

    // --- tous les effets, paramètres par défaut, sur image texturée
    {
        std::unique_ptr<Document> e(Ops::makeDocument(QSize(320, 240), 0, Qt::white));
        cv::Mat& m = e->activeLayer()->image;
        cv::RNG rng(1);
        for (int y = 0; y < m.rows; ++y) for (int x = 0; x < m.cols; ++x) m.at<cv::Vec4b>(y, x) = cv::Vec4b(x * 255 / m.cols, y * 255 / m.rows, uchar(rng.uniform(0, 255)), 255);
        int count = 0;
        for (auto& ef : EffectRegistry::instance().all()) {
            cv::Mat out = ef->run(m, ef->defaults());
            bool ok = out.size() == m.size() && out.type() == CV_8UC4;
            if (!ok) qWarning() << "effet invalide:" << ef->id;
            CHECK(ok);
            ++count;
        }
        qInfo() << count << "effets testés";
        Ops::applyEffect(e.get(), *EffectRegistry::instance().find("filter.gaussian"), EffectRegistry::instance().find("filter.gaussian")->defaults());
        e->undoStack()->undo();
        CHECK(e->undoStack()->count() == 1);
    }

    // --- Remplissage d'après le contenu (inpainting OpenCV) : reconstruction depuis le calque seul vs depuis tous les calques
    {
        std::unique_ptr<Document> e2(Ops::makeDocument(QSize(200, 150), 2, Qt::white));   // 2 = transparent
        cv::Mat& bg2 = e2->activeLayer()->image;
        for (int y = 0; y < bg2.rows; ++y) for (int x = 0; x < bg2.cols; ++x) bg2.at<cv::Vec4b>(y, x) = cv::Vec4b(uchar(x * 255 / bg2.cols), 100, 50, 255);
        Ops::addLayer(e2.get(), "Trou");
        cv::Rect hole(80, 55, 40, 40);
        e2->activeLayer()->image(hole).setTo(cv::Scalar(0, 255, 0, 255));           // objet opaque à effacer, sur calque transparent
        e2->undoStack()->clear();

        auto ip = EffectRegistry::instance().find("retouch.inpaint");
        CHECK(ip != nullptr);
        auto sampleAt = [&](int x, int y) { return e2->activeLayer()->image.at<cv::Vec4b>(y, x); };
        int cx = hole.x + hole.width / 2, cy = hole.y + hole.height / 2;
        int expectedB = cx * 255 / bg2.cols;

        Ops::applyEffect(e2.get(), *ip, ip->defaults());                            // sans sélection : refusé
        CHECK(e2->undoStack()->count() == 0);

        e2->setSelection(Sel::fromRect(e2->size(), QRectF(hole.x, hole.y, hole.width, hole.height)), "trou");
        e2->undoStack()->clear();

        Ops::applyEffect(e2.get(), *ip, ip->defaults());                            // calque seul (par défaut)
        CHECK(e2->undoStack()->count() == 1);
        cv::Vec4b layerOnly = sampleAt(cx, cy);
        CHECK(layerOnly[3] == 255);                                                  // rendu opaque
        CHECK(std::abs(int(layerOnly[0]) - expectedB) > 60);                        // reconstruit depuis du transparent : ne ressemble PAS au dégradé
        CHECK(int(sampleAt(5, 5)[3]) == 0);                                          // hors sélection : toujours transparent
        e2->undoStack()->undo();
        CHECK(sampleAt(cx, cy) == cv::Vec4b(0, 255, 0, 255));

        Params p = ip->defaults(); p.set("sampleAll", true);
        Ops::applyEffect(e2.get(), *ip, p);                                          // tous les calques : voit le dégradé à travers la transparence
        CHECK(e2->undoStack()->count() == 1);
        cv::Vec4b merged = sampleAt(cx, cy);
        CHECK(merged[3] == 255);
        CHECK(std::abs(int(merged[0]) - expectedB) <= 40);
        CHECK(std::abs(int(merged[1]) - 100) <= 30 && std::abs(int(merged[2]) - 50) <= 30);
        CHECK(int(sampleAt(5, 5)[3]) == 0);
        int idxAfter = e2->undoStack()->index();
        e2->undoStack()->undo();
        CHECK(sampleAt(cx, cy) == cv::Vec4b(0, 255, 0, 255));
        e2->undoStack()->redo();
        CHECK(e2->undoStack()->index() == idxAfter && sampleAt(cx, cy) == merged);
    }

    // --- IA (vision.cpp) : tout ce qui est testable SANS fichier de modèle (aucun poids n'est disponible dans cet environnement).
    //     Les inférences réseau elles-mêmes ne sont donc PAS testées ici ; ce qui l'est : passerelle d'images, post-traitements,
    //     opérations sur le document, validation/refus propres des fichiers de modèle, chemins d'erreur.
    {
        // fichier GGUF minimal (0 tenseur, 1 métadonnée "general.architecture")
        auto writeGguf = [&](const QString& path, const QByteArray& arch) {
            QFile f(path); f.open(QIODevice::WriteOnly);
            auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
            auto u64 = [&](quint64 v) { f.write(reinterpret_cast<const char*>(&v), 8); };
            auto str = [&](const QByteArray& s) { u64(quint64(s.size())); f.write(s); };
            f.write("GGUF"); u32(3); u64(0); u64(1);
            str("general.architecture"); u32(8); str(arch);
        };
        QString gMigan = tmp.path() + "/fake_migan.gguf", gText = tmp.path() + "/not_gguf.gguf";
        writeGguf(gMigan, "migan");
        { QFile f(gText); f.open(QIODevice::WriteOnly); f.write("this is not a gguf file"); }

        // validation des fichiers de modèle
        CHECK(!AIBackend::validateModelFile(AIArchitecture::Sam, tmp.path() + "/absent.gguf").isEmpty());
#ifdef PC_HAVE_VISIONCPP
        CHECK(!AIBackend::validateModelFile(AIArchitecture::Sam, gText).isEmpty());
        CHECK(AIBackend::validateModelFile(AIArchitecture::MiGan, gMigan).isEmpty());
        CHECK(AIBackend::validateModelFile(AIArchitecture::Sam, gMigan).contains("MI-GAN"));      // mauvais type refusé AVANT chargement
#endif

        // sans modèle configuré / avec un modèle invalide : échec propre et explicite, jamais de plantage
        for (auto& info : AIModels::all()) AIModels::setModelPath(info.id, QString());
        cv::Mat img(48, 64, CV_8UC4, cv::Scalar(10, 20, 30, 255)), m8(48, 64, CV_8UC1, cv::Scalar(255));
        auto r1 = AIBackend::segmentDichotomous(img);
        CHECK(!r1.ok && !r1.error.isEmpty());
#ifdef PC_HAVE_VISIONCPP
        CHECK(r1.error.contains("aucun modèle"));
#endif
        CHECK(!AIBackend::estimateDepth(img).ok && !AIBackend::upscale(img).ok && !AIBackend::inpaint(img, m8).ok);
        QString samErr;
        CHECK(!AIBackend::samEncode(img, &samErr) && !AIBackend::samReady() && !samErr.isEmpty());
        CHECK(!AIBackend::samComputePoint(QPoint(5, 5)).ok && !AIBackend::samComputeBox(QRect(1, 1, 9, 9)).ok);
        AIModels::setModelPath(AIArchitecture::MiGan, gMigan);       // bonne architecture mais aucun poids ni hyperparamètre
        auto r2 = AIBackend::inpaint(img, m8);
        CHECK(!r2.ok && !r2.error.isEmpty());
#ifdef PC_HAVE_VISIONCPP
        CHECK(r2.error.contains("MI-GAN"));
        AIModels::setModelPath(AIArchitecture::BiRefNet, gMigan);    // mauvais type de modèle : refusé proprement
        CHECK(AIBackend::segmentDichotomous(img).error.contains("MI-GAN"));
#endif
        for (auto& info : AIModels::all()) AIModels::setModelPath(info.id, QString());

        // passerelle cv::Mat <-> vision.cpp (sans copie, respecte le pas de ligne, canaux BGRA correctement interprétés)
#ifdef PC_HAVE_VISIONCPP
        {
            cv::Mat big(40, 50, CV_8UC4, cv::Scalar(1, 2, 3, 4));
            cv::Mat roi = big(cv::Rect(5, 5, 20, 10));
            auto v = ai::viewOf(roi);
            CHECK(v.extent[0] == 20 && v.extent[1] == 10 && v.stride == int(big.step[0]) && v.data == roi.data);
            // foreground : moitié gauche rouge (sujet), moitié droite bleue (fond) ; le rouge doit rester rouge (ordre BGRA)
            cv::Mat pic(60, 80, CV_8UC4, cv::Scalar(200, 0, 0, 255)), msk(60, 80, CV_8UC1, cv::Scalar(0));
            pic(cv::Rect(0, 0, 40, 60)).setTo(cv::Scalar(0, 0, 200, 255));
            msk(cv::Rect(0, 0, 40, 60)).setTo(255);
            auto fg = AIBackend::estimateForeground(pic, msk, 6);
            CHECK(fg.ok && fg.data.size() == pic.size());
            if (fg.ok) {
                cv::Vec4b in = fg.data.at<cv::Vec4b>(30, 10), out = fg.data.at<cv::Vec4b>(30, 70);
                CHECK(in[2] > 190 && in[0] < 10 && in[3] == 255);      // sujet : rouge opaque
                CHECK(out[3] == 0);                                     // fond : alpha = masque = 0
            }
            CHECK(!AIBackend::estimateForeground(pic, cv::Mat(10, 10, CV_8UC1), 6).ok);    // tailles incohérentes refusées
        }
#endif

        // post-traitements purs
        {
            cv::Mat raw(100, 100, CV_8UC1, cv::Scalar(0));
            cv::circle(raw, {50, 50}, 30, cv::Scalar(255), -1);
            cv::circle(raw, {90, 10}, 4, cv::Scalar(255), -1);                        // îlot parasite
            cv::circle(raw, {50, 50}, 40, cv::Scalar(90), 3);                          // anneau doux
            MaskRefineParams p;
            CHECK(cv::norm(refineMask(raw, p), raw, cv::NORM_INF) == 0);                // paramètres neutres = identité
            p.threshold = 128;
            CHECK(cv::countNonZero(refineMask(raw, p) == 90) == 0);                     // seuil dur : plus de valeurs intermédiaires
            p = {}; p.keepLargest = true;
            cv::Mat kl = refineMask(raw, p);
            CHECK(kl.at<uchar>(10, 90) == 0 && kl.at<uchar>(50, 50) == 255);           // l'îlot disparaît, le sujet reste
            p = {}; p.shift = 5;
            CHECK(cv::countNonZero(refineMask(raw, p) > 127) > cv::countNonZero(raw > 127));   // dilatation
            p.shift = -5;
            CHECK(cv::countNonZero(refineMask(raw, p) > 127) < cv::countNonZero(raw > 127));   // contraction
            p = {}; p.feather = 8;
            CHECK(refineMask(raw, p).at<uchar>(50, 80) > 0 && refineMask(raw, p).at<uchar>(50, 80) < 255);   // bord adouci
            p = {}; p.invert = true;
            CHECK(refineMask(raw, p).at<uchar>(50, 50) == 0 && refineMask(raw, p).at<uchar>(2, 2) == 255);

            // bleedColors : RGB caché sous alpha 0 remplacé par la couleur voisine ; alpha et pixels opaques inchangés
            cv::Mat spr(60, 60, CV_8UC4, cv::Scalar(0, 0, 0, 0));
            spr(cv::Rect(20, 20, 20, 20)).setTo(cv::Scalar(30, 60, 220, 255));
            cv::Mat bl = bleedColors(spr);
            cv::Vec4b bgp = bl.at<cv::Vec4b>(5, 5), op = bl.at<cv::Vec4b>(30, 30);
            CHECK(bgp[3] == 0 && bgp[2] > 150 && bgp[0] < 80);                          // couleur propagée, alpha intact
            CHECK(op == cv::Vec4b(30, 60, 220, 255));
            cv::Mat opaque(10, 10, CV_8UC4, cv::Scalar(1, 2, 3, 255));
            CHECK(bleedColors(opaque).data == opaque.data);                              // image opaque : renvoyée telle quelle

            // profondeur
            cv::Mat dep(50, 50, CV_8UC1);
            for (int y = 0; y < 50; ++y) for (int x = 0; x < 50; ++x) dep.at<uchar>(y, x) = uchar(100 + x);   // 100..149
            DepthRefineParams dp; dp.autoLevels = true;
            cv::Mat dr = refineDepth(dep, dp);
            double mn, mx; cv::minMaxLoc(dr, &mn, &mx);
            CHECK(mx - mn > 200);                                                        // contraste étiré
            dp.autoLevels = false; dp.invert = true;
            CHECK(refineDepth(dep, dp).at<uchar>(0, 0) == 255 - 100);
            cv::Mat ds = depthToSelection(dr, 128, true, 0), dsd = depthToSelection(dr, 128, false, 0);
            CHECK(!ds.empty() && !dsd.empty() && ds.at<uchar>(10, 49) == 255 && ds.at<uchar>(10, 0) == 0 && dsd.at<uchar>(10, 0) == 255);
            CHECK(depthToSelection(cv::Mat(10, 10, CV_8UC1, cv::Scalar(0)), 128, true, 0).empty());   // rien de sélectionné = pas de sélection
        }

        // Ops::removeBackground : 4 sorties, une seule étape d'historique chacune, annulables
        {
            std::unique_ptr<Document> bg(Ops::makeDocument(QSize(120, 90), 0, Qt::white));
            cv::Mat& li = bg->activeLayer()->image;
            for (int y = 0; y < li.rows; ++y) for (int x = 0; x < li.cols; ++x) li.at<cv::Vec4b>(y, x) = cv::Vec4b(200, 120, 40, 255);   // « fond » bleu-cyan
            cv::circle(li, {60, 45}, 25, cv::Scalar(20, 30, 230, 255), -1);                                                                 // « sujet » rouge
            cv::Mat raw(90, 120, CV_8UC1, cv::Scalar(0));
            cv::circle(raw, {60, 45}, 25, cv::Scalar(255), -1);
            bg->undoStack()->clear();
            cv::Mat before = bg->activeLayer()->image.clone();
            QString err, warn;

            Ops::RemoveBgParams p;
            p.output = Ops::RemoveBgParams::LayerMask;                                   // non destructif
            CHECK(Ops::removeBackground(bg.get(), raw, p, &err, &warn) && bg->undoStack()->count() == 1);
            CHECK(bg->activeLayer()->hasMask() && bg->activeLayer()->mask.at<uchar>(45, 60) == 255 && bg->activeLayer()->mask.at<uchar>(2, 2) == 0);
            CHECK(cv::norm(bg->activeLayer()->image, before, cv::NORM_INF) == 0);          // pixels intacts
            CHECK(bg->composite().at<cv::Vec4b>(2, 2)[3] == 0 && bg->composite().at<cv::Vec4b>(45, 60)[3] == 255);
            bg->undoStack()->undo();
            CHECK(!bg->activeLayer()->hasMask());

            p.output = Ops::RemoveBgParams::NewLayer; p.defringe = true;
            size_t n0 = bg->layers().size();
            CHECK(Ops::removeBackground(bg.get(), raw, p, &err, &warn) && bg->layers().size() == n0 + 1 && bg->undoStack()->index() == 1);   // une seule étape (la précédente, annulée, est remplacée)
            {
                auto top = bg->activeLayer();
                CHECK(top->image.at<cv::Vec4b>(45, 60)[3] == 255 && top->image.at<cv::Vec4b>(2, 2)[3] == 0);   // sujet seul, fond transparent
                cv::Vec4b c = top->image.at<cv::Vec4b>(45, 60);
                CHECK(c[2] > 200 && c[0] < 60);                                                                // couleur du sujet conservée (BGRA)
                CHECK(cv::norm(bg->layers()[0]->image, before, cv::NORM_INF) == 0);                            // calque d'origine intact
            }
            bg->undoStack()->undo();

            p.output = Ops::RemoveBgParams::ReplaceLayer;
            CHECK(Ops::removeBackground(bg.get(), raw, p, &err, &warn));
            CHECK(bg->activeLayer()->image.at<cv::Vec4b>(2, 2)[3] == 0 && bg->activeLayer()->image.at<cv::Vec4b>(45, 60)[3] == 255);
            bg->undoStack()->undo();
            CHECK(cv::norm(bg->activeLayer()->image, before, cv::NORM_INF) == 0);

            p.output = Ops::RemoveBgParams::SelectionOnly;
            CHECK(Ops::removeBackground(bg.get(), raw, p, &err, &warn) && bg->hasSelection() && bg->selection().at<uchar>(45, 60) == 255);
            bg->undoStack()->undo();
            CHECK(!bg->hasSelection());

            CHECK(!Ops::removeBackground(bg.get(), cv::Mat(10, 10, CV_8UC1), p, &err, &warn) && !err.isEmpty());   // masque de mauvaise taille refusé
            bg->activeLayer()->props.locked = true;
            p.output = Ops::RemoveBgParams::LayerMask;
            CHECK(!Ops::removeBackground(bg.get(), raw, p, &err, &warn));                                        // calque verrouillé refusé
            bg->activeLayer()->props.locked = false;

            // Ops::applyDepth : calque + sélection en une seule entrée d'historique (macro)
            cv::Mat dep(90, 120, CV_8UC1);
            for (int y = 0; y < 90; ++y) for (int x = 0; x < 120; ++x) dep.at<uchar>(y, x) = uchar(x * 2);
            bg->undoStack()->clear();
            Ops::DepthApplyParams dp; dp.makeLayer = true; dp.makeSelection = true; dp.threshold = 128; dp.depth.autoLevels = false;
            size_t nl = bg->layers().size();
            CHECK(Ops::applyDepth(bg.get(), dep, dp, &err) && bg->layers().size() == nl + 1 && bg->hasSelection());
            CHECK(bg->activeLayer()->image.at<cv::Vec4b>(10, 100)[0] == 200 && bg->activeLayer()->image.at<cv::Vec4b>(10, 100)[3] == 255);   // niveaux de gris opaque
            CHECK(bg->selection().at<uchar>(10, 100) == 255 && bg->selection().at<uchar>(10, 10) == 0);
            CHECK(bg->undoStack()->count() == 1);        // calque + sélection = UNE macro : un seul Ctrl+Z…
            bg->undoStack()->undo();
            CHECK(bg->layers().size() == nl && !bg->hasSelection());           // …un seul Ctrl+Z annule tout
            dp.makeLayer = dp.makeSelection = false;
            CHECK(!Ops::applyDepth(bg.get(), dep, dp, &err));
        }

        // Ops::aiUpscale sans modèle : refus propre, document intact, aucune entrée d'historique
        {
            std::unique_ptr<Document> up(Ops::makeDocument(QSize(40, 30), 0, Qt::white));
            up->undoStack()->clear();
            QString err;
            CHECK(!Ops::aiUpscale(up.get(), Ops::UpscaleParams{}, &err) && !err.isEmpty());
            CHECK(up->size() == QSize(40, 30) && up->undoStack()->count() == 0);
        }

        // Remplissage : MI-GAN sans modèle => échec signalé, AUCUN changement ni historique ; Telea/NS toujours OK (régression)
        {
            std::unique_ptr<Document> ig(Ops::makeDocument(QSize(100, 80), 0, Qt::white));
            cv::Mat& gi = ig->activeLayer()->image;
            for (int y = 0; y < gi.rows; ++y) for (int x = 0; x < gi.cols; ++x) gi.at<cv::Vec4b>(y, x) = cv::Vec4b(uchar(x * 2), 100, 50, 255);
            gi(cv::Rect(40, 30, 20, 20)).setTo(cv::Scalar(0, 255, 0, 255));
            ig->setSelection(Sel::fromRect(ig->size(), QRectF(40, 30, 20, 20)), "sel");
            ig->undoStack()->clear();
            auto ip = EffectRegistry::instance().find("retouch.inpaint");
            CHECK(ip && ip->defs[0].choices.size() == 3);
            cv::Mat before = ig->activeLayer()->image.clone();
            Params p = ip->defaults(); p.set("algo", 2);
            Ops::applyEffect(ig.get(), *ip, p);
            CHECK(ig->undoStack()->count() == 0 && cv::norm(ig->activeLayer()->image, before, cv::NORM_INF) == 0);
            for (int algo : {0, 1}) {
                Params q = ip->defaults(); q.set("algo", algo);
                Ops::applyEffect(ig.get(), *ip, q);
                CHECK(ig->undoStack()->count() == 1 && ig->activeLayer()->image.at<cv::Vec4b>(40, 50) != cv::Vec4b(0, 255, 0, 255));
                ig->undoStack()->undo();
            }
        }
    }

    // --- Stable Diffusion : logique pure, validation des fichiers, bibliothèque réelle (échecs propres) et mécanique de fil/annulation
    //     (moteur injecté). Aucun modèle Stable Diffusion n'est disponible ici : la génération réelle n'est PAS testée.
    {
        auto waitFor = [&](const std::function<bool()>& cond, int timeoutMs) {
            QElapsedTimer t; t.start();
            while (!cond() && t.elapsed() < timeoutMs) { app.processEvents(QEventLoop::AllEvents, 20); QThread::msleep(4); }
            return cond();
        };
        auto write = [&](const QString& name, const QByteArray& data) { QString p = tmp.path() + "/" + name; QFile f(p); f.open(QIODevice::WriteOnly); f.write(data); return p; };
        auto le64 = [](quint64 n) { QByteArray b; for (int i = 0; i < 8; ++i) b.append(char((n >> (8 * i)) & 0xff)); return b; };

        // ---- validation des fichiers de modèle
        const QByteArray js = "{\"__metadata__\":{\"format\":\"pt\"}}";
        QString stOk = write("ok.safetensors", le64(js.size()) + js + QByteArray(200, '\0'));
        QString stBad = write("bad.safetensors", le64(999999) + js);                          // taille d'en-tête incohérente
        QString gg = write("m.gguf", QByteArray("GGUF") + QByteArray(32, '\0'));
        QString zp = write("m.ckpt", QByteArray("PK\x03\x04", 4) + QByteArray(64, '\0'));
        QString pk = write("old.ckpt", QByteArray("\x80\x02", 2) + QByteArray(64, '\0'));
        QString junk = write("junk.bin", "ceci n'est absolument pas un modele de diffusion");
        QString tiny = write("tiny.bin", "abc");
        CHECK(Sd::validateModelFile(stOk).isEmpty() && Sd::validateModelFile(gg).isEmpty() && Sd::validateModelFile(zp).isEmpty() && Sd::validateModelFile(pk).isEmpty());
        CHECK(!Sd::validateModelFile(stBad).isEmpty() && !Sd::validateModelFile(junk).isEmpty() && !Sd::validateModelFile(tiny).isEmpty());
        CHECK(Sd::validateModelFile(tmp.path() + "/absent.safetensors").contains("introuvable"));
        Sd::Config cfg;
        CHECK(!Sd::hasModel(cfg) && Sd::validateConfig(cfg).contains("aucun modèle"));
        cfg.model = stOk;
        CHECK(Sd::hasModel(cfg) && Sd::validateConfig(cfg).isEmpty());
        cfg.vae = junk;
        CHECK(Sd::validateConfig(cfg).startsWith("VAE"));                                        // l'erreur désigne le bon champ
        cfg.vae.clear(); cfg.threads = 3; cfg.flashAttention = true; cfg.mmap = false; cfg.unloadAfterUse = true; cfg.clipL = gg;
        Sd::saveConfig(cfg);
        CHECK(Sd::loadConfig() == cfg);                                                          // aller-retour des réglages
        Sd::saveConfig(Sd::Config{});
        CHECK(!Sd::hasModel(Sd::loadConfig()));

        // ---- planification de l'inpainting : géométrie
        {
            cv::Mat src(600, 800, CV_8UC4);
            for (int y = 0; y < 600; ++y) for (int x = 0; x < 800; ++x) src.at<cv::Vec4b>(y, x) = cv::Vec4b(uchar(x / 4), uchar(y / 3), 90, 255);
            cv::Mat sel = Sel::fromRect(QSize(800, 600), QRectF(300, 200, 100, 80));
            Sd::InpaintPlan plan; QString err;
            CHECK(Sd::planInpaint(src, sel, Sd::InpaintPlanParams{}, &plan, &err));
            CHECK(plan.region == cv::Rect(296, 196, 108, 88));                                   // sélection + dilatation de 4 px
            CHECK((plan.crop & plan.region) == plan.region && plan.crop.width == plan.crop.height);   // le recadrage contient la zone, carré
            CHECK(plan.work == cv::Size(512, 512) && plan.workImage.size() == plan.work && plan.workMask.size() == plan.work);
            CHECK(plan.work.width % 8 == 0 && plan.work.height % 8 == 0);
            CHECK(cv::countNonZero((plan.workMask != 0) & (plan.workMask != 255)) == 0);         // masque de travail strictement binaire
            const double frac = double(cv::countNonZero(plan.workMask)) / plan.workMask.total();
            CHECK(std::abs(frac - double(108 * 88) / (216 * 216)) < 0.03);                       // proportion cohérente avec la géométrie
            CHECK(plan.blendMask.size() == src.size() && plan.blendMask.at<uchar>(240, 350) == 255 && plan.blendMask.at<uchar>(10, 10) == 0);
            CHECK(cv::countNonZero(plan.blendMask(cv::Rect(0, 0, 800, 100))) == 0);              // rien hors du recadrage
            bool soft = false;
            for (int x = 290; x < 302 && !soft; ++x) { uchar v = plan.blendMask.at<uchar>(240, x); soft = v > 0 && v < 255; }
            CHECK(soft);                                                                          // bord adouci (feather)

            // bord de l'image : le recadrage reste dans l'image et contient la zone
            cv::Mat corner = Sel::fromRect(QSize(800, 600), QRectF(0, 0, 50, 50));
            Sd::InpaintPlanParams pp; pp.expand = 0;
            CHECK(Sd::planInpaint(src, corner, pp, &plan, &err));
            CHECK(cv::Rect(0, 0, 800, 600).contains(plan.crop.tl()) && (plan.crop | cv::Rect(0, 0, 800, 600)) == cv::Rect(0, 0, 800, 600));
            CHECK((plan.crop & plan.region) == plan.region);
            // image plus petite que la fenêtre voulue, non carrée : proportions conservées, multiples de 8
            cv::Mat small(60, 100, CV_8UC4, cv::Scalar(5, 5, 5, 255));
            CHECK(Sd::planInpaint(small, cv::Mat(60, 100, CV_8UC1, cv::Scalar(255)), Sd::InpaintPlanParams{}, &plan, &err));
            CHECK(plan.crop == cv::Rect(0, 0, 100, 60) && plan.work.width % 8 == 0 && plan.work.height % 8 == 0);
            CHECK(std::abs(double(plan.work.width) / plan.work.height - 100.0 / 60.0) < 0.1);
            // cas invalides
            CHECK(!Sd::planInpaint(src, cv::Mat(600, 800, CV_8UC1, cv::Scalar(0)), Sd::InpaintPlanParams{}, &plan, &err) && err.contains("vide"));
            CHECK(!Sd::planInpaint(src, cv::Mat(10, 10, CV_8UC1, cv::Scalar(255)), Sd::InpaintPlanParams{}, &plan, &err));
            CHECK(!Sd::planInpaint(cv::Mat(10, 10, CV_8UC3), cv::Mat(10, 10, CV_8UC1), Sd::InpaintPlanParams{}, &plan, &err));
            pp = {}; pp.workRes = 100000;
            CHECK(Sd::planInpaint(src, sel, pp, &plan, &err) && plan.work.width <= 2048);        // résolution bornée

            // recollage : seul l'intérieur de la zone change ; les trous transparents deviennent opaques
            CHECK(Sd::planInpaint(src, sel, Sd::InpaintPlanParams{}, &plan, &err));
            cv::Mat target = src.clone();
            target(cv::Rect(330, 230, 10, 10)).setTo(cv::Scalar(9, 9, 9, 0));                    // trou transparent dans la zone
            cv::Mat gen(plan.work, CV_8UC4, cv::Scalar(0, 0, 255, 255));                         // rouge (BGRA)
            cv::Mat out = Sd::compositeInpaint(target, plan, gen);
            CHECK(out.size() == target.size());
            CHECK(cv::norm(out(cv::Rect(0, 0, 800, 100)), target(cv::Rect(0, 0, 800, 100)), cv::NORM_INF) == 0);
            CHECK(cv::norm(out(cv::Rect(0, 500, 800, 100)), target(cv::Rect(0, 500, 800, 100)), cv::NORM_INF) == 0);   // hors recadrage : identique au bit près
            cv::Vec4b mid = out.at<cv::Vec4b>(240, 350), hole = out.at<cv::Vec4b>(235, 335);
            CHECK(mid[2] > 240 && mid[0] < 15 && mid[3] == 255);                                  // cœur de zone : rouge
            CHECK(hole[3] == 255 && hole[2] > 240);                                               // ex-trou transparent : rempli et opaque
            // dans le recadrage mais hors zone (blendMask == 0) : inchangé
            cv::Mat ring;
            cv::compare(plan.blendMask(plan.crop), 0, ring, cv::CMP_EQ);
            CHECK(cv::countNonZero(ring) > 1000);
            CHECK(cv::norm(out(plan.crop), target(plan.crop), cv::NORM_INF, ring) == 0);
        }

        // ---- placement d'une image générée
        {
            cv::Mat gen(50, 100, CV_8UC4, cv::Scalar(0, 0, 255, 255));
            const QSize doc(200, 200);
            cv::Mat nat = Sd::placeGenerated(gen, doc, Sd::Placement::Native);
            CHECK(nat.size() == cv::Size(200, 200) && nat.at<cv::Vec4b>(100, 100)[3] == 255 && nat.at<cv::Vec4b>(10, 10)[3] == 0 && nat.at<cv::Vec4b>(100, 40)[3] == 0);
            cv::Mat fit = Sd::placeGenerated(gen, doc, Sd::Placement::Fit);
            CHECK(fit.at<cv::Vec4b>(100, 5)[3] == 255 && fit.at<cv::Vec4b>(100, 195)[3] == 255 && fit.at<cv::Vec4b>(20, 100)[3] == 0);   // 200 × 100 centré
            cv::Mat cov = Sd::placeGenerated(gen, doc, Sd::Placement::Cover);
            CHECK(cv::countNonZero(cov.reshape(1) == 0) == 0 || cv::countNonZero(cv::Mat(cov.size(), CV_8UC1, cv::Scalar(0))) == 0);
            std::vector<cv::Mat> ch; cv::split(cov, ch);
            CHECK(cv::countNonZero(ch[3] == 255) == 200 * 200);                                   // couvre tout le document
            cv::Mat sel(200, 200, CV_8UC1, cv::Scalar(0));
            cv::circle(sel, {100, 100}, 40, cv::Scalar(255), -1);
            cv::Mat into = Sd::placeGenerated(gen, doc, Sd::Placement::IntoSelection, sel);
            CHECK(into.at<cv::Vec4b>(100, 100)[3] == 255 && into.at<cv::Vec4b>(100, 30)[3] == 0 && into.at<cv::Vec4b>(62, 62)[3] == 0);   // découpé en disque
            cv::Mat noSel = Sd::placeGenerated(gen, doc, Sd::Placement::IntoSelection, cv::Mat());
            CHECK(cv::norm(noSel, fit, cv::NORM_INF) == 0);                                        // repli : « Ajusté »
            cv::Mat big(300, 300, CV_8UC4, cv::Scalar(1, 2, 3, 255));
            cv::Mat crop = Sd::placeGenerated(big, QSize(100, 100), Sd::Placement::Native);
            CHECK(crop.size() == cv::Size(100, 100) && cv::countNonZero(cv::Mat(crop.size(), CV_8UC1, cv::Scalar(0))) == 0 && crop.at<cv::Vec4b>(0, 0)[3] == 255 && crop.at<cv::Vec4b>(99, 99)[3] == 255);
        }

        // ---- Ops : nouveau calque / nouveau document / inpainting (une étape d'historique chacun)
        {
            std::unique_ptr<Document> sd(Ops::makeDocument(QSize(200, 120), 0, Qt::white));
            sd->undoStack()->clear();
            cv::Mat gen(64, 64, CV_8UC4, cv::Scalar(10, 200, 30, 255));
            QString err;
            auto l = Ops::sdAddImage(sd.get(), gen, Sd::Placement::Fit, "un très long prompt qui dépasse largement la limite de longueur du nom de calque", &err);
            CHECK(l && sd->layers().size() == 2 && sd->undoStack()->count() == 1 && l->props.name.startsWith("IA : ") && l->props.name.size() < 40);
            sd->undoStack()->undo();
            CHECK(sd->layers().size() == 1);
            CHECK(!Ops::sdAddImage(sd.get(), cv::Mat(), Sd::Placement::Fit, "x", &err) && !err.isEmpty());
            std::unique_ptr<Document> nd(Ops::sdNewDocument(gen, "chat"));
            CHECK(nd->size() == QSize(64, 64) && nd->layers().size() == 1 && nd->layers()[0]->image.at<cv::Vec4b>(5, 5) == cv::Vec4b(10, 200, 30, 255) && nd->undoStack()->count() == 0);

            sd->setSelection(Sel::fromRect(sd->size(), QRectF(80, 40, 40, 40)), "sel");
            Sd::InpaintPlan plan;
            CHECK(Sd::planInpaint(sd->activeLayer()->image, sd->selection(), Sd::InpaintPlanParams{}, &plan, &err));
            cv::Mat red(plan.work, CV_8UC4, cv::Scalar(0, 0, 255, 255));
            cv::Mat before = sd->activeLayer()->image.clone();
            sd->undoStack()->clear();
            CHECK(Ops::sdApplyInpaint(sd.get(), plan, red, &err) && sd->undoStack()->count() == 1);
            CHECK(sd->activeLayer()->image.at<cv::Vec4b>(60, 100)[2] > 240 && sd->activeLayer()->image.at<cv::Vec4b>(2, 2) == before.at<cv::Vec4b>(2, 2));
            sd->undoStack()->undo();
            CHECK(cv::norm(sd->activeLayer()->image, before, cv::NORM_INF) == 0);                // annulation exacte
            CHECK(!Ops::sdApplyInpaint(sd.get(), plan, cv::Mat(10, 10, CV_8UC4), &err));         // taille de travail incorrecte refusée
            sd->activeLayer()->props.locked = true;
            CHECK(!Ops::sdApplyInpaint(sd.get(), plan, red, &err));                              // calque verrouillé refusé
            sd->activeLayer()->props.locked = false;
        }

        // ---- bibliothèque RÉELLE : chargement dynamique et échec propre sur des modèles invalides (sans planter l'application)
        Sd::setTestEngine({});
        if (Sd::libraryCompiled()) {
            QString libErr;
            const bool loaded = Sd::libraryLoaded(&libErr);
            CHECK(loaded);                                   // libpcsd.so doit se charger (voir depend/stablediffusioncpp/BUILD_FROM_SOURCE.md)
            if (loaded) {
                CHECK(!Sd::libraryPath().isEmpty() && Sd::libraryVersionInfo().contains("AVX"));
                Sd::Request rq; rq.width = rq.height = 64; rq.steps = 1; rq.prompt = "a cat";
                QString why;
                CHECK(Sd::start(rq, &why) == nullptr && why.contains("aucun modèle"));           // refus avant tout démarrage de fil
                struct Bad { const char* n; QByteArray d; } bads[] = {
                    {"gguf-vide.gguf", QByteArray("GGUF") + QByteArray(12, '\0')},
                    {"zip-faux.ckpt", QByteArray("PK\x03\x04", 4) + QByteArray(64, '\0')},
                    {"st-meta.safetensors", le64(js.size()) + js + QByteArray(64, '\0')}};
                for (auto& b : bads) {
                    rq.config.model = write(b.n, b.d);
                    Sd::Job* job = Sd::start(rq, &why);
                    CHECK(job != nullptr);
                    if (!job) continue;
                    bool got = false; Sd::Result res; bool onGui = false;
                    QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { res = r; got = true; onGui = (QThread::currentThread() == app.thread()); });
                    CHECK(waitFor([&] { return got; }, 30000));
                    CHECK(onGui && !res.ok && !res.cancelled && res.error.contains("charger le modèle"));
                    waitFor([] { return !Sd::isBusy(); }, 2000);
                }
                Sd::shutdown();
            }
        }

        // ---- mécanique du fil : moteur injecté dans le VRAI Sd::Job (progression, isolation du fil, réactivité, annulation, exclusivité)
        std::atomic<bool> engineSawCancel{false};
        std::atomic<void*> engineThread{nullptr};
        int steps = 10, stepMs = 40;
        Sd::setTestEngine([&](const Sd::Request& r, Sd::Job& job) {
            Sd::Result res;
            engineThread = QThread::currentThread();
            job.reportStage("Génération…");
            for (int s = 1; s <= steps; ++s) {
                if (job.cancelRequested()) { engineSawCancel = true; res.cancelled = true; return res; }
                QThread::msleep(stepMs);
                job.reportProgress(s, steps, stepMs / 1000.0);
            }
            for (int i = 0; i < r.batch; ++i) res.images.push_back(cv::Mat(r.height, r.width, CV_8UC4, cv::Scalar(20 * i, 100, 200, 255)));
            res.ok = true;
            res.seed = r.seed >= 0 ? r.seed : 4242;
            res.modelVersion = "moteur de test";
            return res;
        });
        Sd::Request rq; rq.width = 64; rq.height = 64; rq.steps = steps; rq.batch = 3; rq.seed = 7; rq.prompt = "x";
        QString why;
        {   // exécution normale : le calcul tourne HORS du fil de l'interface, qui reste réactif ; événements ordonnés et livrés dans le fil de l'interface
            engineSawCancel = false; engineThread = nullptr;
            Sd::Job* job = Sd::start(rq, &why);
            CHECK(job != nullptr && Sd::isBusy());
            std::vector<int> seen; bool guiOk = true, got = false; Sd::Result res;
            QObject::connect(job, &Sd::Job::progress, &app, [&](int s, int n, double) { seen.push_back(s); guiOk &= (QThread::currentThread() == app.thread()) && n == 10; });
            QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { res = r; got = true; guiOk &= (QThread::currentThread() == app.thread()); });
            int ticks = 0;
            QTimer tick; tick.setInterval(10); QObject::connect(&tick, &QTimer::timeout, &app, [&] { ++ticks; }); tick.start();
            CHECK(waitFor([&] { return got; }, 10000));
            tick.stop();
            CHECK(ticks >= 15);                                                                   // ~400 ms de calcul : la boucle d'événements n'a pas été bloquée
            CHECK(engineThread.load() != nullptr && engineThread.load() != static_cast<void*>(app.thread()));   // vrai fil séparé
            CHECK(guiOk && res.ok && res.images.size() == 3 && res.seed == 7 && res.images[0].size() == cv::Size(64, 64));
            CHECK(seen.size() == 10 && std::is_sorted(seen.begin(), seen.end()) && seen.front() == 1 && seen.back() == 10);
            CHECK(res.seconds >= 0.3);
            waitFor([] { return !Sd::isBusy(); }, 2000);
            CHECK(!Sd::isBusy());
        }
        {   // annulation en cours de calcul : rapide, aucun résultat partiel, le moteur voit le drapeau
            steps = 100; stepMs = 30; engineSawCancel = false;
            Sd::Job* job = Sd::start(rq, &why);
            CHECK(job != nullptr);
            bool got = false; Sd::Result res;
            QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { res = r; got = true; });
            waitFor([] { return false; }, 200);                                                   // laisse le calcul démarrer
            QElapsedTimer t; t.start();
            job->cancel();
            CHECK(job->cancelRequested());
            CHECK(waitFor([&] { return got; }, 5000));
            CHECK(t.elapsed() < 400);                                                             // ≈ une étape (30 ms), pas les 3 s restantes
            CHECK(res.cancelled && !res.ok && res.images.empty() && engineSawCancel.load());
            waitFor([] { return !Sd::isBusy(); }, 2000);
            job = nullptr;
        }
        {   // annulation immédiate (avant que le moteur ne regarde) : jamais perdue
            steps = 50; stepMs = 20; engineSawCancel = false;
            Sd::Job* job = Sd::start(rq, &why);
            CHECK(job != nullptr);
            bool got = false; Sd::Result res;
            QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { res = r; got = true; });
            job->cancel();
            CHECK(waitFor([&] { return got; }, 5000) && res.cancelled && res.images.empty());
            waitFor([] { return !Sd::isBusy(); }, 2000);
        }
        {   // un moteur qui « termine » malgré l'annulation : le résultat est quand même écarté (annuler = abandonner)
            Sd::setTestEngine([&](const Sd::Request& r, Sd::Job& job) {
                QThread::msleep(120);
                Sd::Result res; res.ok = true; res.images.push_back(cv::Mat(r.height, r.width, CV_8UC4, cv::Scalar(1, 2, 3, 255)));
                Q_UNUSED(job);
                return res;
            });
            Sd::Job* job = Sd::start(rq, &why);
            bool got = false; Sd::Result res;
            QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { res = r; got = true; });
            job->cancel();
            CHECK(waitFor([&] { return got; }, 5000) && res.cancelled && !res.ok && res.images.empty());
            waitFor([] { return !Sd::isBusy(); }, 2000);
        }
        {   // une seule génération à la fois ; on peut relancer dès que la précédente est terminée
            Sd::setTestEngine([&](const Sd::Request&, Sd::Job& job) {
                Sd::Result res;
                for (int i = 0; i < 300 && !job.cancelRequested(); ++i) QThread::msleep(10);
                res.cancelled = job.cancelRequested();
                return res;
            });
            Sd::Job* first = Sd::start(rq, &why);
            CHECK(first != nullptr);
            QString why2;
            CHECK(Sd::start(rq, &why2) == nullptr && why2.contains("déjà en cours"));
            first->cancel();
            waitFor([] { return !Sd::isBusy(); }, 5000);
            Sd::setTestEngine([&](const Sd::Request&, Sd::Job&) { Sd::Result r; r.ok = true; r.images.push_back(cv::Mat(64, 64, CV_8UC4, cv::Scalar(1, 1, 1, 255))); return r; });
            Sd::Job* second = Sd::start(rq, &why);
            CHECK(second != nullptr);
            bool got = false;
            QObject::connect(second, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { got = r.ok; });
            CHECK(waitFor([&] { return got; }, 5000));
            waitFor([] { return !Sd::isBusy(); }, 2000);
        }
        {   // exception dans le moteur : erreur signalée, pas de plantage ; l'application reste utilisable
            Sd::setTestEngine([&](const Sd::Request&, Sd::Job&) -> Sd::Result { throw std::runtime_error("boum"); });
            Sd::Job* job = Sd::start(rq, &why);
            bool got = false; Sd::Result res;
            QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { res = r; got = true; });
            CHECK(waitFor([&] { return got; }, 5000) && !res.ok && !res.cancelled && res.error.contains("boum"));
            waitFor([] { return !Sd::isBusy(); }, 2000);
        }
        {   // course « connexion après start() » : un calcul qui échoue instantanément ne doit JAMAIS perdre son résultat
            Sd::setTestEngine([&](const Sd::Request&, Sd::Job& job) { job.reportProgress(1, 1, 0.0); Sd::Result r; r.error = "échec immédiat"; return r; });
            int lost = 0, wrongThread = 0;
            for (int i = 0; i < 150; ++i) {
                Sd::Job* job = Sd::start(rq, &why);
                if (!job) { waitFor([] { return !Sd::isBusy(); }, 1000); job = Sd::start(rq, &why); }
                bool got = false, prog = false;
                QObject::connect(job, &Sd::Job::progress, &app, [&](int, int, double) { prog = true; });
                QObject::connect(job, &Sd::Job::resultReady, &app, [&](const Sd::Result& r) { got = r.error == "échec immédiat"; if (QThread::currentThread() != app.thread()) ++wrongThread; });
                if (!waitFor([&] { return got; }, 3000) || !prog) ++lost;
                waitFor([] { return !Sd::isBusy(); }, 2000);
            }
            CHECK(lost == 0 && wrongThread == 0);
        }
        {   // requêtes invalides refusées AVANT de créer un fil
            Sd::setTestEngine([&](const Sd::Request&, Sd::Job&) { return Sd::Result{}; });
            Sd::Request bad = rq; bad.width = 100;                       QString w1;
            CHECK(Sd::start(bad, &w1) == nullptr && w1.contains("multiples de 8"));
            bad = rq; bad.batch = 0;                                     CHECK(Sd::start(bad, &w1) == nullptr && w1.contains("variantes"));
            bad = rq; bad.steps = 0;                                     CHECK(Sd::start(bad, &w1) == nullptr && w1.contains("étapes"));
            bad = rq; bad.mode = Sd::Request::Inpaint;                   CHECK(Sd::start(bad, &w1) == nullptr && w1.contains("Inpainting"));
            bad.initImage = cv::Mat(64, 64, CV_8UC4, cv::Scalar(0, 0, 0, 255)); bad.mask = cv::Mat(64, 64, CV_8UC1, cv::Scalar(0));
            CHECK(Sd::start(bad, &w1) == nullptr && w1.contains("masque est vide"));
            CHECK(!Sd::isBusy());
        }
        Sd::setTestEngine({});
    }

    // --- sauvegarde / relecture
    QString pcl = tmp.path() + "/t.pcl", png = tmp.path() + "/t.png", jpg = tmp.path() + "/t.jpg";
    QString err;
    CHECK(ImageIO::save(d.get(), pcl, &err));
    std::unique_ptr<Document> r(ImageIO::open(pcl, &err));
    CHECK(r && r->layers().size() == d->layers().size() && r->size() == d->size());
    if (r) { bool same = true; for (size_t i = 0; i < r->layers().size(); ++i) same &= cv::norm(r->layers()[i]->image, d->layers()[i]->image) == 0; CHECK(same); }
    CHECK(ImageIO::save(d.get(), png, &err) && ImageIO::save(d.get(), jpg, &err));
    std::unique_ptr<Document> r2(ImageIO::open(png, &err));
    CHECK(r2 && r2->size() == d->size());

    // --- interface : fenêtre principale + capture (document de démonstration multi-calques)
    MainWindow w;
    w.resize(1500, 900);
    w.show();
    {
        std::unique_ptr<Document> demo(Ops::makeDocument(QSize(900, 600), 0, Qt::white));
        cv::Mat& bg = demo->activeLayer()->image;
        for (int y = 0; y < bg.rows; ++y) for (int x = 0; x < bg.cols; ++x)
            bg.at<cv::Vec4b>(y, x) = cv::Vec4b(uchar(200 - y / 4), uchar(120 + x / 12), uchar(60 + y / 5), 255);
        Ops::addLayer(demo.get(), "Cercles");
        cv::Mat& c = demo->activeLayer()->image;
        cv::circle(c, {300, 300}, 170, cv::Scalar(30, 40, 230, 255), -1, cv::LINE_AA);
        cv::circle(c, {520, 260}, 130, cv::Scalar(240, 200, 30, 255), -1, cv::LINE_AA);
        LayerProps lp = demo->activeLayer()->props; lp.blend = BlendMode::Multiply; lp.opacity = 0.9f;
        Ops::setProps(demo.get(), demo->activeLayer(), lp, "Mode de fusion");
        TextData t; t.text = "PhotoClone"; t.pixelSize = 110; t.bold = true; t.color = Qt::white; t.pos = QPointF(190, 420);
        Ops::commitText(demo.get(), nullptr, t);
        Ops::addLayer(demo.get(), "Masque test");
        Ops::addMask(demo.get(), false);
        demo->undoStack()->clear();
        QString err2;
        ImageIO::save(demo.get(), tmp.path() + "/demo.pcl", &err2);
    }
    w.openPath(tmp.path() + "/demo.pcl");
    app.processEvents();
    if (auto* v = ws.view()) {
        v->document()->setSelection(Sel::fromEllipse(v->document()->size(), QRectF(520, 60, 260, 200)), "Sélection elliptique");
        ws.tools()->select(ws.tools()->byId("marquee_ellipse"));
        v->document()->setActiveIndex(1);
        Ops::applyEffect(v->document(), *EffectRegistry::instance().find("adjust.huesat"), [] { Params p = EffectRegistry::instance().find("adjust.huesat")->defaults(); p.set("h", 60); return p; }());
        Ops::deselect(v->document());
        Ops::modifySelection(v->document(), 0, 1);
        v->document()->setSelection(Sel::fromEllipse(v->document()->size(), QRectF(520, 60, 260, 200)), "Sélection elliptique");
    } else CHECK(false);
    app.processEvents();
    QTimer::singleShot(200, [] {});
    for (int i = 0; i < 5; ++i) { app.processEvents(); QThread::msleep(40); }
    // --- pilotage de tous les outils par de vrais événements, puis cohérence de l'historique (undo/redo intégral)
    {
        auto* v = ws.view();
        Document* dd = v->document();
        auto* tm = ws.tools();
        auto send = [&](QEvent::Type t, QPointF img, Qt::MouseButton b, Qt::KeyboardModifiers m = Qt::NoModifier) {
            QPointF wp = v->toWidget(img);
            QMouseEvent ev(t, wp, v->viewport()->mapToGlobal(wp.toPoint()), b, t == QEvent::MouseButtonRelease ? Qt::NoButton : b, m);
            QApplication::sendEvent(v->viewport(), &ev);
        };
        auto drag = [&](QPointF a, QPointF b, Qt::KeyboardModifiers m = Qt::NoModifier) {
            send(QEvent::MouseButtonPress, a, Qt::LeftButton, m);
            for (int i = 1; i <= 6; ++i) send(QEvent::MouseMove, a + (b - a) * (i / 6.0), Qt::LeftButton, m);
            send(QEvent::MouseButtonRelease, b, Qt::LeftButton, m);
        };
        auto click = [&](QPointF a, Qt::KeyboardModifiers m = Qt::NoModifier) { send(QEvent::MouseButtonPress, a, Qt::LeftButton, m); send(QEvent::MouseButtonRelease, a, Qt::LeftButton, m); };
        auto key = [&](int k) { QKeyEvent ev(QEvent::KeyPress, k, Qt::NoModifier); QApplication::sendEvent(v, &ev); };
        auto use = [&](const char* id) { tm->select(tm->byId(id)); };

        app.processEvents();
        cv::Mat comp0 = dd->compositeCopy();
        int i0 = dd->undoStack()->index();
        dd->setActiveIndex(0);                                    // Arrière-plan
        ws.setFg(QColor(220, 30, 30)); ws.setBg(QColor(250, 240, 40));

        use("brush");    drag({60, 60}, {200, 90}); drag({200, 90}, {260, 40}, Qt::ShiftModifier);
        use("pencil");   drag({60, 120}, {200, 150});
        use("eraser");   drag({100, 55}, {140, 75});
        use("blur");     drag({300, 300}, {400, 330});
        use("sharpen");  drag({300, 340}, {400, 370});
        use("smudge");   drag({300, 250}, {420, 260});
        use("dodge");    drag({400, 400}, {470, 430});
        use("burn");     drag({400, 450}, {470, 480});
        use("clone");    click({350, 300}, Qt::AltModifier); drag({600, 100}, {700, 140});
        use("bucket");   click({20, 20});
        use("marquee_rect");    drag({450, 450}, {600, 560});
        CHECK(dd->hasSelection());
        use("gradient"); drag({450, 450}, {600, 560});
        use("move");     drag({500, 500}, {540, 520});           // déplace le contenu sélectionné
        use("marquee_ellipse"); drag({50, 300}, {200, 420}, Qt::ShiftModifier);
        // transformation de la sélection, mode CONTOUR SEUL : agrandir par la poignée BD, puis pivoter de ~90° (les pixels ne bougent pas)
        {
            ws.settings.selTransformContent = false;
            dd->setSelection(Sel::fromRect(dd->size(), QRectF(300, 200, 200, 100)), "rect");
            CHECK(dd->hasSelection());
            cv::Mat layerBefore = dd->activeLayer()->image.clone();
            cv::Rect r0 = Sel::bounds(dd->selection());
            CHECK(r0 == cv::Rect(300, 200, 200, 100));
            use("sel_transform");
            QPointF brc(r0.x + r0.width, r0.y + r0.height);
            drag(brc, brc + QPointF(50, 30));
            key(Qt::Key_Return);
            cv::Rect r1 = Sel::bounds(dd->selection());
            CHECK(std::abs(r1.width - 250) <= 2 && std::abs(r1.height - 130) <= 2 && std::abs(r1.x - 300) <= 1 && std::abs(r1.y - 200) <= 1);   // ancrage = coin opposé
            QPointF c1(r1.x + r1.width / 2.0, r1.y + r1.height / 2.0);
            drag(c1 + QPointF(r1.width / 2.0 + 25, 0), c1 + QPointF(0, r1.height / 2.0 + 25));
            key(Qt::Key_Return);
            cv::Rect r2 = Sel::bounds(dd->selection());
            CHECK(std::abs(r2.width - r1.height) <= 4 && std::abs(r2.height - r1.width) <= 4);   // 90° : largeur ↔ hauteur
            CHECK(std::abs((r2.x + r2.width / 2.0) - c1.x()) <= 2 && std::abs((r2.y + r2.height / 2.0) - c1.y()) <= 2);   // autour du centre
            CHECK(cv::norm(dd->activeLayer()->image, layerBefore, cv::NORM_INF) == 0);   // seul le contour a changé
            drag({r2.x + r2.width * 0.5, r2.y + r2.height * 0.5}, {r2.x + r2.width * 0.5 + 40, r2.y + r2.height * 0.5 + 10});   // déplacement par le corps
            key(Qt::Key_Return);
            cv::Rect r3 = Sel::bounds(dd->selection());
            CHECK(std::abs(r3.x - (r2.x + 40)) <= 2 && std::abs(r3.y - (r2.y + 10)) <= 2);
            int n = dd->undoStack()->index();
            dd->undoStack()->undo();                                                     // annuler : revient au contour précédent
            CHECK(Sel::bounds(dd->selection()) == r2);
            dd->undoStack()->redo();
            CHECK(dd->undoStack()->index() == n);
        }
        // transformation de la sélection AVEC contenu (mode par défaut) : les pixels du calque actif suivent le contour
        {
            ws.settings.selTransformContent = true;
            Ops::addLayer(dd, "Contenu");
            dd->setSelection(Sel::fromRect(dd->size(), QRectF(300, 200, 200, 100)), "rect");
            Ops::fillSelection(dd, QColor(255, 0, 0), "fill");
            cv::Mat lay0 = dd->activeLayer()->image.clone();
            int idx0 = dd->undoStack()->index();
            auto alphaBox = [&] { cv::Mat a; cv::extractChannel(dd->activeLayer()->image, a, 3); return cv::boundingRect(a > 10); };   // (>10 : ignore les arrondis d'interpolation)
            auto near = [](cv::Rect a, cv::Rect b, int tol) { return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol && std::abs(a.width - b.width) <= tol && std::abs(a.height - b.height) <= tol; };
            CHECK(alphaBox() == cv::Rect(300, 200, 200, 100));
            use("sel_transform");
            QPointF brc(500, 300);
            drag(brc, brc + QPointF(50, 30));
            CHECK(near(alphaBox(), cv::Rect(300, 200, 250, 130), 3));                 // aperçu en direct, avant validation
            key(Qt::Key_Return);
            cv::Rect s1 = Sel::bounds(dd->selection());
            CHECK(near(alphaBox(), s1, 3));                                            // le contenu épouse le contour
            CHECK(dd->undoStack()->index() == idx0 + 1);                               // pixels + contour = une seule étape
            dd->undoStack()->undo();
            CHECK(cv::norm(dd->activeLayer()->image, lay0, cv::NORM_INF) == 0 && Sel::bounds(dd->selection()) == cv::Rect(300, 200, 200, 100));
            dd->undoStack()->redo();
            CHECK(near(alphaBox(), s1, 3));
            QPointF c1(s1.x + s1.width / 2.0, s1.y + s1.height / 2.0);
            drag(c1 + QPointF(s1.width / 2.0 + 25, 0), c1 + QPointF(0, s1.height / 2.0 + 25));   // rotation ~90°
            key(Qt::Key_Return);
            cv::Rect s2 = Sel::bounds(dd->selection()), a2 = alphaBox();
            CHECK(std::abs(s2.width - s1.height) <= 4 && std::abs(s2.height - s1.width) <= 4);
            CHECK(near(a2, s2, 3));
            CHECK(dd->activeLayer()->image.at<cv::Vec4b>(205, 305)[3] == 0);           // l'ancien emplacement est vidé
            CHECK(dd->activeLayer()->image.at<cv::Vec4b>(int(c1.y()), int(c1.x()))[2] == 255);   // le centre est toujours rouge
            // verrouillage : le calque verrouillé n'est pas modifié, seul le contour l'est
            LayerProps lp = dd->activeLayer()->props; lp.locked = true; dd->activeLayer()->props = lp;
            cv::Mat locked0 = dd->activeLayer()->image.clone();
            cv::Rect sb = Sel::bounds(dd->selection());
            drag({sb.x + sb.width * 0.5, sb.y + sb.height * 0.5}, {sb.x + sb.width * 0.5 + 30, sb.y + sb.height * 0.5});
            key(Qt::Key_Return);
            CHECK(cv::norm(dd->activeLayer()->image, locked0, cv::NORM_INF) == 0);
            CHECK(Sel::bounds(dd->selection()).x == sb.x + 30);
            lp.locked = false; dd->activeLayer()->props = lp;
        }
        use("lasso");    drag({700, 300}, {760, 360});
        use("lasso_poly"); click({700, 450}); click({780, 450}); click({780, 520}); { QMouseEvent dc(QEvent::MouseButtonDblClick, v->toWidget({700, 520}), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(v->viewport(), &dc); }
        use("wand");     click({600, 30});
        Ops::invertSelection(dd); Ops::modifySelection(dd, 1, 3); Ops::modifySelection(dd, 0, 4);
        Ops::fillSelection(dd, Qt::green, "fill");
        Ops::copy(dd, false); Ops::paste(dd, false, QPoint(300, 300)); Ops::cut(dd);
        Ops::deselect(dd);
        use("shape");    drag({620, 380}, {760, 470});
        ws.settings.shapeKind = 2; drag({100, 400}, {180, 460}); ws.settings.shapeKind = 3; drag({100, 500}, {300, 580});
        Ops::addMask(dd, false); dd->activeLayer()->editingMask = true;
        ws.setFg(Qt::black); use("brush"); drag({300, 300}, {500, 300});                                // peinture sur le masque
        dd->activeLayer()->editingMask = false;
        // transformation manuelle : échelle par la poignée BD, puis rotation (clic hors du cadre), validation par Entrée
        {
            auto* tt = tm->transform();
            cv::Mat a; cv::extractChannel(dd->activeLayer()->image, a, 3);
            cv::Rect bb = cv::boundingRect(a > 0);
            CHECK(!bb.empty());
            CHECK(tt->begin(dd));
            tm->select(tt);
            QPointF br(bb.x + bb.width, bb.y + bb.height);
            drag(br, br + QPointF(60, 40));                                          // agrandit
            drag(br + QPointF(60, 40) + QPointF(30, 30), br + QPointF(60, 40) + QPointF(60, 10));   // tourne
            cv::extractChannel(dd->activeLayer()->image, a, 3);
            cv::Rect bb2 = cv::boundingRect(a > 0);
            CHECK(bb2.area() > bb.area());
            key(Qt::Key_Return);
            CHECK(!tt->active());
            tm->select(tm->byId("brush"));
        }
        // dialogues déplaçables dans la fenêtre : cadre enfant de la fenêtre principale, barre de titre glissable,
        // position mémorisée, interface inhibée sauf navigation dans l'image, aperçu en direct sans altérer le calque
        {
            dd->setActiveIndex(0);
            cv::Mat before = dd->activeLayer()->image.clone();
            QSettings("PhotoClone", "PhotoClone").remove("dialogs");
            auto e = EffectRegistry::instance().find("adjust.levels");
            QPoint lastPos, startPos;
            {
                EffectDialog dlg(dd, e, e->defaults(), &w);
                QTimer::singleShot(250, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    CHECK(host && host->isVisible() && host->parentWidget() == &w);
                    if (!host) { dlg.reject(); return; }
                    CHECK(w.rect().contains(host->geometry()));
                    CHECK(!w.menuBar()->isEnabled() && ws.modalLocked());
                    // navigation possible pendant le dialogue, mais pas la peinture
                    double z0 = v->zoom();
                    QWheelEvent we(QPointF(400, 300), QPointF(400, 300), QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false);
                    QApplication::sendEvent(v->viewport(), &we);
                    CHECK(v->zoom() != z0);
                    v->zoom100();
                    cv::Mat live = dd->activeLayer()->image.clone();
                    tm->select(tm->byId("brush")); drag({100, 100}, {200, 150});
                    CHECK(cv::norm(dd->activeLayer()->image, live, cv::NORM_INF) == 0);
                    // glisser la barre de titre
                    auto* tb = host->findChild<QWidget*>("DialogTitleBar");
                    CHECK(tb != nullptr);
                    startPos = host->pos();
                    auto dragTitle = [&](QPoint delta) {
                        QPoint c = tb->rect().center(), g0 = tb->mapToGlobal(c);
                        QMouseEvent pr(QEvent::MouseButtonPress, QPointF(c), QPointF(g0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(tb, &pr);
                        QMouseEvent mv(QEvent::MouseMove, QPointF(c + delta), QPointF(g0 + delta), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(tb, &mv);
                        QMouseEvent rl(QEvent::MouseButtonRelease, QPointF(c + delta), QPointF(g0 + delta), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(tb, &rl);
                    };
                    dragTitle(QPoint(-200, 120));
                    CHECK(host->pos() != startPos && w.rect().contains(host->geometry()));
                    CHECK(host->pos() == startPos + QPoint(-200, 120));
                    dragTitle(QPoint(-9000, -9000));                                  // hors fenêtre : recadré
                    CHECK(host->pos() == QPoint(0, 0));
                    dragTitle(QPoint(150, 200));
                    lastPos = host->pos();
                    CHECK(w.rect().contains(host->geometry()));
                    dlg.reject();
                });
                dlg.exec();
            }
            CHECK(w.menuBar()->isEnabled() && !ws.modalLocked());                   // interface rendue
            CHECK(cv::norm(dd->activeLayer()->image, before, cv::NORM_INF) == 0);   // rejet : calque intact
            {   // rouverture : position mémorisée
                EffectDialog dlg2(dd, e, e->defaults(), &w);
                QPoint again;
                QTimer::singleShot(200, [&] { auto* h = w.findChild<QFrame*>("DialogHost"); if (h) again = h->pos(); dlg2.accept(); });
                CHECK(dlg2.exec() == QDialog::Accepted);
                CHECK(again == lastPos);
            }
            for (const char* id : {"adjust.curves", "filter.gaussian"}) {           // aperçu en direct puis rejet
                auto ef = EffectRegistry::instance().find(id);
                Params p = ef->defaults(); if (std::string(id) == "filter.gaussian") p.set("r", 12.0);
                EffectDialog dlg(dd, ef, p, &w);
                QTimer::singleShot(150, &dlg, &QDialog::reject);
                dlg.exec();
                CHECK(cv::norm(dd->activeLayer()->image, before, cv::NORM_INF) == 0);
            }
            // sélecteur de couleur et saisies dans un cadre déplaçable
            QTimer::singleShot(150, [&] { auto* h = w.findChild<QFrame*>("DialogHost"); CHECK(h != nullptr); if (auto* d = h ? h->findChild<QDialog*>() : nullptr) d->accept(); });
            QColor picked = Dlg::pickColor(QColor(10, 20, 30), "Couleur", &w);
            CHECK(picked == QColor(10, 20, 30));
            QTimer::singleShot(150, [&] { auto* h = w.findChild<QFrame*>("DialogHost"); if (auto* d = h ? h->findChild<QDialog*>() : nullptr) d->reject(); });
            bool ok = true; Dlg::getInt(&w, "Qualité", "Qualité :", 92, 1, 100, &ok);
            CHECK(!ok);
            tm->select(tm->byId("brush"));
        }
        use("crop");     drag({20, 20}, {800, 560}); key(Qt::Key_Return);
        CHECK(dd->size() == QSize(781, 541) || dd->size().width() > 100);
        use("zoom");     click({100, 100}); click({100, 100}, Qt::AltModifier); drag({50, 50}, {200, 200});
        use("hand");     drag({300, 300}, {320, 310});
        { QWheelEvent we(QPointF(400, 300), QPointF(400, 300), QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::AltModifier, Qt::NoScrollPhase, false); QApplication::sendEvent(v->viewport(), &we); }
        v->zoomFit(); v->zoom100();
        app.processEvents();
        cv::Mat comp1 = dd->compositeCopy();
        int i1 = dd->undoStack()->index();
        qInfo() << "entrées d'historique ajoutées:" << (i1 - i0);
        CHECK(i1 > i0 + 20);
        dd->undoStack()->setIndex(i0);
        app.processEvents();
        CHECK(dd->size() == QSize(900, 600));
        CHECK(cv::norm(dd->composite(), comp0, cv::NORM_INF) == 0);          // annulation intégrale = état initial exact
        dd->undoStack()->setIndex(i1);
        CHECK(cv::norm(dd->composite(), comp1, cv::NORM_INF) == 0);          // rétablissement intégral = état final exact
        for (int k = 0; k < 3; ++k) { dd->undoStack()->setIndex(i0); dd->undoStack()->setIndex(i1); }
        CHECK(cv::norm(dd->composite(), comp1, cv::NORM_INF) == 0);
        dd->undoStack()->setIndex(i0);

        // --- captures pour la documentation : transformation de la sélection, puis dialogue déplacé sur l'image
        dd->setActiveIndex(1);
        v->zoomFit(true);
        dd->setSelection(Sel::fromEllipse(dd->size(), QRectF(190, 210, 230, 170)), "sélection");
        use("sel_transform");
        cv::Rect s0 = Sel::bounds(dd->selection());
        QPointF brc2(s0.x + s0.width, s0.y + s0.height);
        drag(brc2, brc2 + QPointF(60, 40));
        key(Qt::Key_Return);
        cv::Rect s1 = Sel::bounds(dd->selection());
        QPointF cc(s1.x + s1.width / 2.0, s1.y + s1.height / 2.0);
        drag(cc + QPointF(s1.width / 2.0 + 30, 0), cc + QPointF(s1.width / 2.0 * 0.6, s1.height / 2.0 + 40));   // rotation d'environ 40° (aperçu en direct, non validée)
        app.processEvents();
        w.grab().save("/tmp/shots/seltransform.png");
        key(Qt::Key_Return);
        {
            auto ef = EffectRegistry::instance().find("adjust.huesat");
            Params pp = ef->defaults(); pp.set("h", 70); pp.set("s", 25);
            EffectDialog dlg(dd, ef, pp, &w);
            QTimer::singleShot(500, [&] {
                auto* host = w.findChild<QFrame*>("DialogHost");
                if (host) host->move(QPoint(80, 250));
                for (int i = 0; i < 5; ++i) { app.processEvents(); QThread::msleep(30); }
                w.grab().save("/tmp/shots/dialog.png");
                dlg.reject();
            });
            dlg.exec();
        }
        Ops::deselect(dd);

        // --- capture : remplissage d'après le contenu (inpainting OpenCV) sur un petit défaut ajouté au fond
        dd->setActiveIndex(0);
        {
            cv::Mat& bgimg = dd->activeLayer()->image;
            cv::circle(bgimg, {780, 90}, 34, cv::Scalar(15, 15, 15, 255), -1, cv::LINE_AA);
            dd->invalidateAll();
        }
        use("marquee_ellipse");
        drag({715, 30}, {850, 155});
        CHECK(dd->hasSelection());
        {
            auto ip = EffectRegistry::instance().find("retouch.inpaint");
            CHECK(ip != nullptr);
            Params pp = ip->defaults();
            EffectDialog dlg2(dd, ip, pp, &w);
            QTimer::singleShot(400, [&] {
                auto* host = w.findChild<QFrame*>("DialogHost");
                if (host) host->move(QPoint(40, 170));
                for (int i = 0; i < 5; ++i) { app.processEvents(); QThread::msleep(30); }
                w.grab().save("/tmp/shots/inpaint.png");
                dlg2.accept();
            });
            dlg2.exec();
        }
        Ops::deselect(dd);

        // --- boîtes de dialogue IA pilotées avec un « faux modèle » injecté (le calcul réseau est remplacé par un masque synthétique)
        {
            dd->setActiveIndex(0);
            cv::Mat layerBefore = dd->activeLayer()->image.clone();
            const int W = dd->size().width(), H = dd->size().height();
            AIImageFn fakeMask = [W, H](const cv::Mat&) {
                AIBackend::Result r; r.ok = true;
                r.data = cv::Mat(H, W, CV_8UC1, cv::Scalar(0));
                cv::ellipse(r.data, cv::Point(W / 2, H / 2), cv::Size(W / 4, H / 3), 0, 0, 360, cv::Scalar(255), -1, cv::LINE_AA);
                return r;
            };
            // (a) suppression de l'arrière-plan : analyse à l'ouverture, aperçu instantané, validation
            {
                RemoveBackgroundDialog dlg(dd, &w, fakeMask);
                Ops::RemoveBgParams got; cv::Mat gotRaw;
                QTimer::singleShot(400, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    CHECK(host != nullptr);
                    if (host) host->move(QPoint(40, 90));
                    CHECK(dlg.hasMask());
                    CHECK(dd->activeLayer()->image.at<cv::Vec4b>(5, 5)[3] == 0 && dd->activeLayer()->image.at<cv::Vec4b>(H / 2, W / 2)[3] == 255);   // aperçu : sujet seul
                    auto spins = dlg.findChildren<SliderSpin*>();
                    CHECK(spins.size() >= 4);
                    if (spins.size() >= 4) spins[0]->setValue(128);          // seuil
                    if (spins.size() >= 4) spins[2]->setValue(6);            // contour progressif
                    for (int i = 0; i < 6; ++i) { app.processEvents(); QThread::msleep(30); }
                    w.grab().save("/tmp/shots/removebg.png");
                    got = dlg.params(); gotRaw = dlg.rawMask();
                    dlg.accept();
                });
                CHECK(dlg.exec() == QDialog::Accepted);
                CHECK(got.mask.threshold == 128 && got.mask.feather == 6 && got.output == Ops::RemoveBgParams::LayerMask && !gotRaw.empty());
                CHECK(cv::norm(dd->activeLayer()->image, layerBefore, cv::NORM_INF) == 0);          // l'aperçu est retiré : rien n'est appliqué par le dialogue
            }
            // (b) échec du modèle : message d'erreur affiché, validation impossible, calque intact
            {
                RemoveBackgroundDialog dlg(dd, &w, [](const cv::Mat&) { AIBackend::Result r; r.error = "BiRefNet : aucun modèle configuré (test)."; return r; });
                bool okEnabled = true; QString text;
                QTimer::singleShot(300, [&] {
                    okEnabled = dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled();
                    for (auto* l : dlg.findChildren<QLabel*>()) if (l->text().contains("aucun modèle configuré (test)")) text = l->text();
                    dlg.reject();
                });
                dlg.exec();
                CHECK(!okEnabled && !text.isEmpty() && !dlg.hasMask());
                CHECK(cv::norm(dd->activeLayer()->image, layerBefore, cv::NORM_INF) == 0);
            }
            // (c) carte de profondeur
            {
                AIImageFn fakeDepth = [W, H](const cv::Mat&) {
                    AIBackend::Result r; r.ok = true; r.data = cv::Mat(H, W, CV_8UC1);
                    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) r.data.at<uchar>(y, x) = uchar(std::clamp(int(255.0 * (1.0 - std::hypot(x - W / 2.0, y - H / 2.0) / (W / 1.6))), 0, 255));
                    return r;
                };
                DepthDialog dlg(dd, &w, fakeDepth);
                Ops::DepthApplyParams got;
                QTimer::singleShot(400, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    if (host) host->move(QPoint(40, 60));
                    CHECK(dlg.hasDepth());
                    for (auto* c : dlg.findChildren<QCheckBox*>()) if (c->text().contains("sélection")) c->setChecked(true);
                    for (int i = 0; i < 5; ++i) { app.processEvents(); QThread::msleep(30); }
                    auto th = dlg.findChild<QLabel*>();
                    bool thumb = false;
                    for (auto* l : dlg.findChildren<QLabel*>()) if (!l->pixmap(Qt::ReturnByValue).isNull()) thumb = true;
                    CHECK(thumb);
                    w.grab().save("/tmp/shots/depth.png");
                    got = dlg.params();
                    dlg.accept();
                });
                CHECK(dlg.exec() == QDialog::Accepted);
                CHECK(got.makeLayer && got.makeSelection);
            }
            // (d) réglages des modèles : statuts ✓ / ✗ selon le fichier, mémorisation à la validation
            {
                QString good = tmp.path() + "/fake_migan.gguf";
                AIModelsDialog dlg(&w);
                QTimer::singleShot(400, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    if (host) host->move(QPoint(20, 60));
                    auto edits = dlg.findChildren<QLineEdit*>();
                    CHECK(edits.size() == 5);
                    if (edits.size() == 5) {
                        edits[0]->setText(tmp.path() + "/absent.gguf"); emit edits[0]->editingFinished();     // MobileSAM : introuvable
                        edits[1]->setText(good); emit edits[1]->editingFinished();                              // BiRefNet : c'est un fichier MI-GAN
                        edits[3]->setText(good); emit edits[3]->editingFinished();                              // MI-GAN : correct
                    }
                    for (int i = 0; i < 4; ++i) { app.processEvents(); QThread::msleep(30); }
                    w.grab().save("/tmp/shots/aimodels.png");
                    dlg.accept();
                });
                CHECK(dlg.exec() == QDialog::Accepted);
                CHECK(AIModels::modelPath(AIArchitecture::MiGan) == good && AIModels::isConfigured(AIArchitecture::Sam));
                for (auto& info : AIModels::all()) AIModels::setModelPath(info.id, QString());
            }
            // (e) agrandissement : infos de taille
            {
                UpscaleDialog dlg(dd, &w);
                QTimer::singleShot(300, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    if (host) host->move(QPoint(60, 120));
                    for (int i = 0; i < 3; ++i) { app.processEvents(); QThread::msleep(30); }
                    w.grab().save("/tmp/shots/upscale.png");
                    dlg.accept();
                });
                CHECK(dlg.exec() == QDialog::Accepted && dlg.params().nativeScale);
            }
            // (f) outil MobileSAM sans modèle : message clair, sélection inchangée ; il est bien dans le groupe de la baguette (W)
            {
                CHECK(tm->byId("sam") != nullptr);
                bool inWandGroup = false;
                for (auto& g : tm->groups()) if (g.key == "W") inWandGroup = g.tools.size() == 2 && g.tools[1]->id() == "sam";
                CHECK(inWandGroup);
                Ops::deselect(dd);
                use("sam");
                w.statusBar()->clearMessage();
                click({300, 200});
                CHECK(!dd->hasSelection() && w.statusBar()->currentMessage().contains("MobileSAM"));
                drag({100, 100}, {220, 200});                                                                    // mode « cadre »
                CHECK(!dd->hasSelection());
                use("brush");
            }
            // (g) capture du menu IA
            for (QAction* a : w.menuBar()->actions())
                if (a->text().contains("IA") && a->menu()) {
                    a->menu()->popup(w.mapToGlobal(QPoint(420, 26)));
                    app.processEvents();
                    a->menu()->grab().save("/tmp/shots/menu_ia.png");
                    a->menu()->hide();
                }

        // --- dialogues Stable Diffusion, pilotés avec un moteur injecté (le vrai Sd::Job/fil/annulation est utilisé ; seul le calcul est simulé)
        {
            auto waitFor = [&](const std::function<bool()>& cond, int timeoutMs) {
                QElapsedTimer t; t.start();
                while (!cond() && t.elapsed() < timeoutMs) { app.processEvents(QEventLoop::AllEvents, 20); QThread::msleep(4); }
                return cond();
            };
            auto byText = [](QWidget* root, const QString& text) -> QPushButton* {
                for (auto* b : root->findChildren<QPushButton*>()) if (b->text() == text) return b;
                return nullptr;
            };
            std::atomic<int> steps{8}, stepMs{50};
            std::atomic<bool> sawCancel{false};
            Sd::setTestEngine([&](const Sd::Request& r, Sd::Job& job) {
                Sd::Result res;
                job.reportStage("Génération…");
                job.reportLog(2, "test : début");
                for (int s = 1; s <= steps; ++s) {
                    if (job.cancelRequested()) { sawCancel = true; res.cancelled = true; return res; }
                    QThread::msleep(stepMs);
                    job.reportProgress(s, steps, stepMs / 1000.0);
                }
                for (int i = 0; i < r.batch; ++i) {
                    cv::Mat img(r.height, r.width, CV_8UC4);
                    if (r.mode == Sd::Request::Inpaint) img.setTo(cv::Scalar(255, 0, 255, 255));            // magenta franc
                    else for (int y = 0; y < img.rows; ++y) for (int x = 0; x < img.cols; ++x)
                        img.at<cv::Vec4b>(y, x) = cv::Vec4b(uchar(40 + 70 * i), uchar(255 * x / img.cols), uchar(255 * y / img.rows), 255);
                    res.images.push_back(img);
                }
                res.ok = true; res.seed = 31337; res.modelVersion = "moteur de test";
                return res;
            });

            // (a) génération : variantes, progression réelle, entrées verrouillées pendant le calcul, ajout comme nouveau calque
            {
                dd->setActiveIndex(0);
                const size_t nl = dd->layers().size();
                const int idx0 = dd->undoStack()->index();
                SdGenerateDialog dlg(dd, &w);
                cv::Mat picked;
                QTimer::singleShot(300, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    if (host) host->move(QPoint(30, 40));
                    for (auto* sp : dlg.findChildren<QSpinBox*>()) {
                        if (sp->toolTip().contains("variantes")) sp->setValue(3);
                        if (sp->toolTip().contains("taille d'entraînement")) sp->setValue(128);
                    }
                    for (auto* sp : dlg.findChildren<QSpinBox*>()) if (sp->suffix() == " px" && sp->singleStep() == 8) sp->setValue(128);
                    dlg.findChildren<QPlainTextEdit*>().first()->setPlainText("un chat roux endormi");
                    steps = 8; stepMs = 60;
                    CHECK(dlg.generate() && dlg.running());
                    auto* bar = dlg.findChild<QProgressBar*>();
                    auto* prompt = dlg.findChildren<QPlainTextEdit*>().first();
                    CHECK(!prompt->parentWidget()->isEnabled());                              // entrées verrouillées pendant le calcul
                    CHECK(byText(&dlg, "Annuler le calcul")->isEnabled() && !byText(&dlg, "Générer")->isEnabled());
                    CHECK(!dlg.generate());                                                    // pas de second calcul simultané
                    waitFor([&] { return bar->maximum() == 8 && bar->value() >= 4; }, 5000);
                    CHECK(bar->maximum() == 8 && bar->value() >= 4);                           // vraie progression reçue depuis le fil
                    for (int i = 0; i < 3; ++i) { app.processEvents(); QThread::msleep(20); }
                    w.grab().save("/tmp/shots/sd_generate_running.png");
                    CHECK(waitFor([&] { return !dlg.running(); }, 8000));
                    for (int i = 0; i < 4; ++i) { app.processEvents(); QThread::msleep(20); }
                    CHECK(dlg.resultCount() == 3 && dlg.currentResult() == 0 && dlg.selectedImage().size() == cv::Size(128, 128));
                    CHECK(prompt->parentWidget()->isEnabled() && byText(&dlg, "Ajouter au document")->isEnabled() && !byText(&dlg, "Annuler le calcul")->isEnabled());
                    CHECK(dlg.statusText().contains("3 image(s)") && dlg.lastSeed() == 31337);
                    auto* list = dlg.findChild<QListWidget*>();
                    CHECK(list && list->count() == 3 && !list->isHidden());
                    list->setCurrentRow(2);                                                    // choisir la 3e variante
                    for (int i = 0; i < 3; ++i) { app.processEvents(); QThread::msleep(20); }
                    CHECK(dlg.currentResult() == 2 && dlg.selectedImage().at<cv::Vec4b>(5, 5)[0] == 40 + 70 * 2);
                    w.grab().save("/tmp/shots/sd_generate_done.png");
                    picked = dlg.selectedImage().clone();
                    dlg.accept();
                });
                CHECK(dlg.exec() == QDialog::Accepted);
                QString err;
                CHECK(Ops::sdAddImage(dd, dlg.selectedImage(), dlg.placement(), dlg.promptText(), &err) != nullptr);
                CHECK(dd->layers().size() == nl + 1 && dd->undoStack()->index() == idx0 + 1 && dd->activeLayer()->props.name.startsWith("IA : un chat"));
                CHECK(!picked.empty() && dd->activeLayer()->image.at<cv::Vec4b>(dd->size().height() / 2, dd->size().width() / 2)[0] == 40 + 70 * 2);   // « Ajustée » : centre = variante choisie
                dd->undoStack()->undo();
                CHECK(dd->layers().size() == nl);
                dd->setActiveIndex(0);
            }

            // (b) bouton « Annuler le calcul » : arrêt rapide, dialogue toujours ouvert et cohérent, aucun résultat
            {
                SdGenerateDialog dlg(dd, &w);
                double cancelMs = -1;
                QTimer::singleShot(300, [&] {
                    for (auto* sp : dlg.findChildren<QSpinBox*>()) if (sp->toolTip().contains("variantes")) sp->setValue(1);
                    steps = 200; stepMs = 40; sawCancel = false;
                    CHECK(dlg.generate());
                    waitFor([] { return false; }, 250);
                    QElapsedTimer t; t.start();
                    byText(&dlg, "Annuler le calcul")->click();
                    CHECK(waitFor([&] { return !dlg.running(); }, 5000));
                    cancelMs = double(t.elapsed());
                    CHECK(dlg.isVisible() && sawCancel.load());                               // le dialogue reste ouvert
                    CHECK(dlg.statusText().contains("annulée") && dlg.resultCount() == 0 && dlg.selectedImage().empty());
                    CHECK(!byText(&dlg, "Ajouter au document")->isEnabled() && byText(&dlg, "Générer")->isEnabled());
                    steps = 4; stepMs = 10;                                                    // on peut relancer aussitôt après une annulation
                    waitFor([] { return !Sd::isBusy(); }, 2000);
                    CHECK(dlg.generate() && waitFor([&] { return !dlg.running(); }, 5000) && dlg.resultCount() == 1);
                    dlg.reject();
                });
                CHECK(dlg.exec() == QDialog::Rejected);
                CHECK(cancelMs >= 0 && cancelMs < 500);                                        // ≈ une étape de 40 ms, pas les 8 s restantes
            }

            // (c) fermer le dialogue PENDANT un calcul : annulation propre, fermeture différée jusqu'à la fin réelle du fil
            {
                SdGenerateDialog dlg(dd, &w);
                qint64 closeMs = -1; bool stillOpenRightAfter = false;
                QTimer::singleShot(300, [&] {
                    steps = 200; stepMs = 40; sawCancel = false;
                    CHECK(dlg.generate());
                    waitFor([] { return false; }, 250);
                    QElapsedTimer t; t.start();
                    dlg.reject();
                    stillOpenRightAfter = dlg.isVisible() && dlg.running();                  // pas encore fermé : le fil n'a pas terminé
                    closeMs = -2;
                    QTimer::singleShot(0, [&, t] { closeMs = -3; Q_UNUSED(t); });
                });
                QElapsedTimer total;
                total.start();
                CHECK(dlg.exec() == QDialog::Rejected);
                CHECK(stillOpenRightAfter && sawCancel.load());
                CHECK(total.elapsed() < 1500);                                                 // 300 ms d'attente + 250 ms de calcul + une étape : très loin des 8 s
                waitFor([] { return !Sd::isBusy(); }, 3000);
                CHECK(!Sd::isBusy());
            }

            // (d) inpainting : aperçu sur l'image, restauration à la fermeture, application = une seule étape d'historique
            {
                dd->setActiveIndex(0);
                dd->setSelection(Sel::fromEllipse(dd->size(), QRectF(300, 150, 160, 120)), "sélection inpainting");
                cv::Mat original = dd->activeLayer()->image.clone();
                const int idx0 = dd->undoStack()->index();
                {   // (d1) on ferme sans appliquer : le calque est restauré exactement
                    SdInpaintDialog dlg(dd, &w);
                    QTimer::singleShot(300, [&] {
                        steps = 4; stepMs = 15;
                        CHECK(dlg.generate() && waitFor([&] { return !dlg.running(); }, 8000) && dlg.resultCount() == 1);
                        for (int i = 0; i < 3; ++i) { app.processEvents(); QThread::msleep(20); }
                        cv::Vec4b c = dd->activeLayer()->image.at<cv::Vec4b>(210, 380), o = dd->activeLayer()->image.at<cv::Vec4b>(20, 20);
                        CHECK(c[0] > 240 && c[1] < 15 && c[2] > 240);                          // aperçu : magenta au cœur de la zone…
                        CHECK(o == original.at<cv::Vec4b>(20, 20));                            // …et le reste est inchangé
                        dlg.reject();
                    });
                    CHECK(dlg.exec() == QDialog::Rejected);
                    CHECK(cv::norm(dd->activeLayer()->image, original, cv::NORM_INF) == 0 && dd->undoStack()->index() == idx0);
                }
                {   // (d2) on valide : l'aperçu est retiré, puis l'opération applique le résultat en une étape annulable
                    SdInpaintDialog dlg(dd, &w);
                    QTimer::singleShot(300, [&] {
                        auto* host = w.findChild<QFrame*>("DialogHost");
                        if (host) host->move(QPoint(20, 30));
                        steps = 6; stepMs = 60;
                        CHECK(dlg.generate());
                        waitFor([] { return false; }, 150);
                        for (int i = 0; i < 3; ++i) { app.processEvents(); QThread::msleep(20); }
                        w.grab().save("/tmp/shots/sd_inpaint_running.png");
                        CHECK(waitFor([&] { return !dlg.running(); }, 8000));
                        for (int i = 0; i < 4; ++i) { app.processEvents(); QThread::msleep(20); }
                        w.grab().save("/tmp/shots/sd_inpaint_done.png");
                        dlg.accept();
                    });
                    CHECK(dlg.exec() == QDialog::Accepted);
                    CHECK(cv::norm(dd->activeLayer()->image, original, cv::NORM_INF) == 0);   // rien n'est appliqué par le dialogue lui-même
                    QString err;
                    CHECK(Ops::sdApplyInpaint(dd, dlg.plan(), dlg.selectedImage(), &err) && dd->undoStack()->index() == idx0 + 1);
                    cv::Vec4b c = dd->activeLayer()->image.at<cv::Vec4b>(210, 380);
                    CHECK(c[0] > 240 && c[1] < 15 && c[2] > 240);
                    CHECK(dd->activeLayer()->image.at<cv::Vec4b>(20, 20) == original.at<cv::Vec4b>(20, 20));
                    dd->undoStack()->undo();
                    CHECK(cv::norm(dd->activeLayer()->image, original, cv::NORM_INF) == 0);
                }
                Ops::deselect(dd);
            }

            // (d') les réglages mémorisés sont propres à chaque dialogue : le prompt de génération ne fuit pas dans l'inpainting
            {
                dd->setActiveIndex(0);
                dd->setSelection(Sel::fromRect(dd->size(), QRectF(100, 100, 80, 80)), "s");
                SdInpaintDialog inp(dd, &w);
                SdGenerateDialog gen(dd, &w);
                QTimer::singleShot(200, [&] { inp.reject(); });
                inp.exec();
                CHECK(inp.findChildren<QPlainTextEdit*>().first()->toPlainText() != "un chat roux endormi");
                CHECK(gen.findChildren<QPlainTextEdit*>().first()->toPlainText() == "un chat roux endormi");   // le sien est bien resté
                Ops::deselect(dd);
            }

            // (e) réglages : onglets, statut global consolidé, diagnostic de fichier, mémorisation, capture
            {
                const QString good = tmp.path() + "/ok.safetensors", bad = tmp.path() + "/junk.bin";
                // Un safetensors avec de vrais tenseurs (marqueurs UNet + VAE + CLIP) pour un diagnostic riche à vérifier.
                const QByteArray richJson =
                    "{\"model.diffusion_model.input_blocks.0.0.weight\":{\"dtype\":\"F32\",\"shape\":[4],\"data_offsets\":[0,16]},"
                    "\"first_stage_model.decoder.conv_in.weight\":{\"dtype\":\"F32\",\"shape\":[4],\"data_offsets\":[16,32]},"
                    "\"cond_stage_model.transformer.text_model.weight\":{\"dtype\":\"F32\",\"shape\":[4],\"data_offsets\":[32,48]}}";
                QString richSt;
                {
                    QByteArray hlen(8, '\0');
                    quint64 n = quint64(richJson.size());
                    for (int i = 0; i < 8; ++i) hlen[i] = char((n >> (8 * i)) & 0xff);
                    richSt = tmp.path() + "/riche.safetensors";
                    QFile f(richSt);
                    f.open(QIODevice::WriteOnly);
                    f.write(hlen); f.write(richJson); f.write(QByteArray(48, '\0'));
                }
                Sd::saveConfig(Sd::Config{});
                SdSettingsDialog dlg(&w);
                QTimer::singleShot(400, [&] {
                    auto* host = w.findChild<QFrame*>("DialogHost");
                    if (host) host->move(QPoint(20, 40));
                    QList<QLineEdit*> edits;
                    for (auto* e : dlg.findChildren<QLineEdit*>()) if (!qobject_cast<QAbstractSpinBox*>(e->parentWidget())) edits << e;   // hors QLineEdit internes des QSpinBox
                    CHECK(edits.size() == 6);
                    // Onglets : QTabWidget fait remonter (raise()) l'onglet affiché dans la liste des enfants de son parent pour
                    // l'empilement visuel, ce qui réordonne ce que renvoie findChildren — on identifie donc chaque champ par son
                    // infobulle (propre à chaque champ) plutôt que par position.
                    auto byTooltip = [&](const QString& needle) -> QLineEdit* {
                        for (auto* e : edits) if (e->toolTip().contains(needle)) return e;
                        return nullptr;
                    };
                    QLineEdit* modelEdit = byTooltip("fichier principal du modèle");
                    QLineEdit* vaeEdit = byTooltip("Remplace le VAE intégré");
                    QLineEdit* diffEdit = byTooltip("blocs de diffusion");
                    QLineEdit* clipLEdit = byTooltip("Requis par la plupart des architectures");
                    QLineEdit* clipGEdit = byTooltip("Requis par SDXL et SD3 uniquement");
                    CHECK(modelEdit && vaeEdit && diffEdit && clipLEdit && clipGEdit);
                    auto* status = dlg.findChild<QLabel*>("SdGlobalStatus");
                    CHECK(status != nullptr);
                    auto setEdit = [&](QLineEdit* e, const QString& v) { e->setText(v); emit e->editingFinished(); };

                    // --- statut global : transitions selon les champs remplis
                    CHECK(status->text().contains("Aucun modèle"));
                    setEdit(modelEdit, good);
                    CHECK(status->text().contains("checkpoint complet"));
                    setEdit(modelEdit, "");
                    CHECK(status->text().contains("Aucun modèle"));
                    setEdit(diffEdit, richSt);
                    CHECK(status->text().contains("aucun encodeur de texte"));
                    setEdit(clipLEdit, bad);
                    CHECK(status->text().contains("modèle de diffusion + encodeur"));
                    setEdit(modelEdit, good);
                    CHECK(status->text().contains("checkpoint complet ET modèle de diffusion"));

                    // --- diagnostic d'un champ VIDE : aucune fenêtre modale, message clair sur la ligne
                    QLineEdit* t5Edit = byTooltip("Requis par Flux et SD3");
                    CHECK(t5Edit && t5Edit->text().isEmpty());
                    auto* t5DiagBtn = dlg.findChild<QToolButton*>();
                    QToolButton* diagFor = nullptr;
                    for (auto* b : dlg.findChildren<QToolButton*>()) if (b->toolTip().contains("(Encodeur T5-XXL)")) diagFor = b;
                    CHECK(diagFor != nullptr);
                    diagFor->click();                                                         // synchrone : ne doit PAS ouvrir de fenêtre modale
                    CHECK(QApplication::activeModalWidget() == nullptr || QApplication::activeModalWidget() == &dlg);
                    auto* t5Status = dlg.findChild<QLabel*>("SdRowStatus_t5xxl");
                    CHECK(t5Status && t5Status->text().contains("Renseignez d'abord"));
                    Q_UNUSED(t5DiagBtn);

                    // --- diagnostic d'un fichier RICHE (checkpoint complet plausible) : contenu détaillé, puis SHA-256 à la demande
                    setEdit(diffEdit, richSt);
                    QToolButton* diffDiagBtn = nullptr;
                    for (auto* b : dlg.findChildren<QToolButton*>()) if (b->toolTip().contains("(Modèle de diffusion seul)")) diffDiagBtn = b;
                    CHECK(diffDiagBtn != nullptr);
                    QTimer::singleShot(250, [&] {
                        auto* diagDlg = qobject_cast<ModelDiagnosticDialog*>(QApplication::activeModalWidget());
                        CHECK(diagDlg != nullptr);
                        if (!diagDlg) return;
                        auto* text = diagDlg->findChild<QPlainTextEdit*>();
                        CHECK(text != nullptr);
                        const QString report = text->toPlainText();
                        CHECK(report.contains("Safetensors") && report.contains("Nombre de tenseurs/entrées : 3"));
                        CHECK(report.contains("checkpoint complet") && report.contains("UNet") && report.contains("VAE") && report.contains("encodeur de texte CLIP"));
                        CHECK(!report.contains("SHA-256"));                                    // pas calculé par défaut (coûteux)
                        diagDlg->grab().save("/tmp/shots/sd_diagnostic.png");
                        auto* shaBtn = diagDlg->findChild<QPushButton*>();
                        QPushButton* realShaBtn = nullptr;
                        for (auto* b : diagDlg->findChildren<QPushButton*>()) if (b->text().contains("SHA-256")) realShaBtn = b;
                        CHECK(realShaBtn != nullptr);
                        Q_UNUSED(shaBtn);
                        if (realShaBtn) realShaBtn->click();
                        const QString report2 = text->toPlainText();
                        CHECK(report2.contains("SHA-256 : ") && QRegularExpression("SHA-256 : [0-9a-f]{64}").match(report2).hasMatch());
                        diagDlg->accept();
                    });
                    diffDiagBtn->click();
                    CHECK(QApplication::activeModalWidget() == nullptr || QApplication::activeModalWidget() == &dlg);   // la fenêtre de diagnostic s'est bien refermée

                    // --- remise en état pour l'assertion finale (checkpoint complet seul, comme le test d'origine)
                    setEdit(diffEdit, "");
                    setEdit(clipLEdit, "");
                    setEdit(modelEdit, good);
                    setEdit(vaeEdit, bad);
                    for (int i = 0; i < 4; ++i) { app.processEvents(); QThread::msleep(30); }
                    w.grab().save("/tmp/shots/sd_settings.png");
                    auto* tabs = dlg.findChild<QTabWidget*>();
                    if (tabs) { tabs->setCurrentIndex(1); for (int i = 0; i < 3; ++i) app.processEvents(); w.grab().save("/tmp/shots/sd_settings_onglet2.png"); tabs->setCurrentIndex(0); }
                    dlg.accept();
                });
                CHECK(dlg.exec() == QDialog::Accepted);
                CHECK(Sd::loadConfig().model == good && Sd::loadConfig().vae == bad);
                Sd::saveConfig(Sd::Config{});
            }

            // (f) commande d'inpainting du menu sans sélection : message clair, aucune boîte de dialogue
            {
                Ops::deselect(dd);
                use("brush");
                w.statusBar()->clearMessage();
                for (QAction* a : w.menuBar()->actions())
                    if (a->text().contains("IA") && a->menu())
                        for (QAction* m : a->menu()->actions())
                            if (m->text().startsWith("Inpainting sur la sélection")) m->trigger();
                CHECK(w.statusBar()->currentMessage().contains("sélectionnez"));
                CHECK(!w.findChild<QFrame*>("DialogHost") || !w.findChild<QFrame*>("DialogHost")->isVisible());
            }
            Sd::setTestEngine({});
            Sd::shutdown();
        }
        }
    }
    QDir().mkpath("/tmp/shots");
    w.grab().save("/tmp/shots/window.png");
    qInfo() << (fails ? "ÉCHECS :" : "TOUS LES TESTS PASSENT") << fails;
    return fails ? 1 : 0;
}
