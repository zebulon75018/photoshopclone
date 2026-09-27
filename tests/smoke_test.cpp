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
#include "ui/MovableDialog.h"
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
    }
    QDir().mkpath("/tmp/shots");
    w.grab().save("/tmp/shots/window.png");
    qInfo() << (fails ? "ÉCHECS :" : "TOUS LES TESTS PASSENT") << fails;
    return fails ? 1 : 0;
}
