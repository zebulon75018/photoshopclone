#include "ImageIO.h"
#include "Document.h"
#include "MatUtil.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(ImageIO) }; }   // traductions hors classes QObject (voir translations/)

namespace ImageIO {
static const quint32 MAGIC = 0x50434C31;   // "PCL1"

// Garde-fous de relecture d'un projet : un .pcl peut venir de n'importe où, et ses dimensions/indices pilotent des
// allocations et des accès mémoire. Bornes volontairement généreuses — scans grand format, photogrammes 8K agrandis, et
// tout ce que l'application sait créer elle-même (jusqu'à 30000 × 30000 px dans les dialogues). MAX_PIXELS = 2^30
// ≈ 1,07 gigapixel (4 Gio par calque BGRA) : c'est aussi la limite par défaut d'OpenCV (CV_IO_MAX_IMAGE_PIXELS),
// imdecode refuserait de toute façon un calque plus grand.
static const qint32 MAX_SIDE = 65536;
static const qint64 MAX_PIXELS = qint64(1) << 30;
static const qint32 MAX_TEXT_PIXEL_SIZE = 2000;   // maximum proposé par le dialogue de texte (Dialogs.cpp)
// Taille maximale du PNG d'un calque dans un .pcl : QByteArray (Qt 5) est indexé par un int. Au-delà, la conversion
// déborderait et le calque serait enregistré tronqué SANS erreur. Marge pour l'en-tête interne de QByteArray.
static const qint64 MAX_PNG_BYTES = std::numeric_limits<int>::max() - 4096;

QString openFilter() {
    return Tr::tr("Toutes les images (*.pcl *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.ppm *.pgm *.gif);;Projet PhotoClone (*.pcl);;Tous les fichiers (*)");
}
QString saveFilter() {
    return Tr::tr("Projet PhotoClone (*.pcl);;PNG (*.png);;JPEG (*.jpg *.jpeg);;TIFF (*.tif *.tiff);;WebP (*.webp);;BMP (*.bmp)");
}
bool isNativeProject(const QString& p) { return p.endsWith(".pcl", Qt::CaseInsensitive); }

static bool encodePng(const cv::Mat& m, QByteArray* out, QString* err) {
    std::vector<uchar> buf;
    try {
        if (!cv::imencode(".png", m, buf, {cv::IMWRITE_PNG_COMPRESSION, 3})) { *err = Tr::tr("encodage PNG impossible."); return false; }
    } catch (const cv::Exception& e) { *err = Tr::tr("encodage PNG impossible (%1).").arg(e.what()); return false; }
    if (qint64(buf.size()) > MAX_PNG_BYTES) {
        *err = Tr::tr("trop volumineux pour un projet .pcl (PNG compressé de %1, limite %2). Réduisez la taille de l'image, "
                       "ou exportez une version aplatie (TIFF).").arg(mu::humanSize(qint64(buf.size())), mu::humanSize(MAX_PNG_BYTES));
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char*>(buf.data()), int(buf.size()));
    return true;
}
static cv::Mat decodePng(const QByteArray& b, int flags) {
    if (b.isEmpty()) return cv::Mat();
    std::vector<uchar> v(b.begin(), b.end());
    try { return cv::imdecode(v, flags); } catch (const cv::Exception&) { return cv::Mat(); }   // PNG corrompu ou mémoire insuffisante
}

// Dimensions déclarées par l'en-tête IHDR d'un PNG, lues SANS le décoder : un calque de mauvaise taille est rejeté
// avant qu'imdecode n'alloue quoi que ce soit (bombe de décompression). Taille nulle si ce n'est pas un PNG.
static cv::Size pngSize(const QByteArray& b) {
    if (b.size() < 24 || !b.startsWith("\x89PNG\r\n\x1a\n") || b.mid(12, 4) != "IHDR") return {};
    const auto* p = reinterpret_cast<const uchar*>(b.constData());
    return {int(qFromBigEndian<quint32>(p + 16)), int(qFromBigEndian<quint32>(p + 20))};
}

// Décode un plan de projet (calque ou masque) en exigeant exactement la taille du document et le type attendu : le
// compositeur indexe image et masque avec les coordonnées du document, sans revérifier.
static cv::Mat decodePlane(const QByteArray& b, int flags, int type, cv::Size expected) {
    if (pngSize(b) != expected) return cv::Mat();
    cv::Mat m = decodePng(b, flags);
    return (m.type() == type && m.size() == expected) ? m : cv::Mat();
}

static bool saveProject(Document* d, const QString& path, QString* err) {
    // Fichier temporaire, substitué à l'ancien projet seulement par commit() : un échec en cours de route (calque trop
    // gros, disque plein) laisse intacte la dernière sauvegarde réussie au lieu de la tronquer.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = f.errorString(); return false; }
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    s << MAGIC << qint32(1) << qint32(d->size().width()) << qint32(d->size().height()) << qint32(d->activeIndex()) << qint32(d->layers().size());
    for (auto& l : d->layers()) {
        QByteArray img, msk;
        QString why;
        if (!encodePng(l->image, &img, &why) || (l->hasMask() && !encodePng(l->mask, &msk, &why))) {
            if (err) *err = Tr::tr("Calque « %1 » : %2").arg(l->props.name, why);
            return false;                                   // sans commit() : le fichier temporaire est abandonné
        }
        s << l->props.name << l->props.visible << l->props.locked << l->props.opacity << qint32(l->props.blend) << l->props.maskEnabled;
        s << img << msk;
        s << l->isText();
        if (l->isText()) {
            const TextData& t = *l->text;
            s << t.text << t.family << qint32(t.pixelSize) << t.bold << t.italic << t.underline << t.color << qint32(t.align) << t.pos;
        }
    }
    if (s.status() != QDataStream::Ok || !f.commit()) { if (err) *err = Tr::tr("Écriture impossible : %1").arg(f.errorString()); return false; }
    return true;
}

static Document* loadProject(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); return nullptr; }
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    auto fail = [&](const QString& m) -> Document* { if (err) *err = m; return nullptr; };
    quint32 magic; qint32 ver, w, h, active, n;
    s >> magic >> ver >> w >> h >> active >> n;
    if (s.status() != QDataStream::Ok || magic != MAGIC || n < 1) return fail(Tr::tr("Fichier de projet invalide."));
    if (ver != 1) return fail(Tr::tr("Version de projet non prise en charge (%1).").arg(ver));
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE || qint64(w) * h > MAX_PIXELS)
        return fail(Tr::tr("Dimensions du projet hors limites (%1 × %2 px ; maximum %3 px de côté et %4 mégapixels).")
                        .arg(w).arg(h).arg(MAX_SIDE).arg(MAX_PIXELS / 1000000));
    const cv::Size size(w, h);
    // Index actif ramené dans les bornes : plusieurs opérations (supprimer, fusionner, déplacer) l'utilisent tel quel.
    Document::Structure st{QSize(w, h), {}, std::clamp(active, 0, n - 1)};
    for (int i = 0; i < n; ++i) {
        LayerProps p; qint32 blend; QByteArray img, msk; bool isText;
        s >> p.name >> p.visible >> p.locked >> p.opacity >> blend >> p.maskEnabled >> img >> msk >> isText;
        if (s.status() != QDataStream::Ok || blend < 0 || blend >= int(BlendMode::Count) || !std::isfinite(p.opacity))
            return fail(Tr::tr("Calque corrompu."));
        p.blend = BlendMode(blend);
        p.opacity = std::clamp(p.opacity, 0.f, 1.f);
        cv::Mat m = decodePlane(img, cv::IMREAD_UNCHANGED, CV_8UC4, size);
        if (m.empty()) return fail(Tr::tr("Calque corrompu."));
        auto l = Layer::create(p.name, m);
        l->props = p;
        if (!msk.isEmpty()) {
            l->mask = decodePlane(msk, cv::IMREAD_GRAYSCALE, CV_8UC1, size);
            if (l->mask.empty()) return fail(Tr::tr("Masque de calque corrompu."));
        }
        if (isText) {
            TextData t; qint32 px, al;
            s >> t.text >> t.family >> px >> t.bold >> t.italic >> t.underline >> t.color >> al >> t.pos;
            t.pixelSize = std::clamp(px, 1, MAX_TEXT_PIXEL_SIZE); t.align = al;
            l->text = t;
        }
        st.layers.push_back(l);
    }
    if (s.status() != QDataStream::Ok) return fail(Tr::tr("Lecture incomplète."));
    std::unique_ptr<Document> d(new Document(QSize(w, h)));   // composite alloué seulement une fois tout le contenu validé
    d->applyStructure(st);
    d->path = path;
    d->undoStack()->clear();
    return d.release();
}

static cv::Mat toBGRA(cv::Mat m) {
    if (m.depth() == CV_16U) m.convertTo(m, CV_8U, 1.0 / 257.0);
    else if (m.depth() == CV_32F || m.depth() == CV_64F) m.convertTo(m, CV_8U, 255.0);
    cv::Mat out;
    switch (m.channels()) {
    case 1: cv::cvtColor(m, out, cv::COLOR_GRAY2BGRA); break;
    case 3: cv::cvtColor(m, out, cv::COLOR_BGR2BGRA); break;
    case 4: out = m; break;
    default: return cv::Mat();
    }
    return out;
}

Document* open(const QString& path, QString* err) {
    if (isNativeProject(path)) return loadProject(path, err);
    cv::Mat m = toBGRA(cv::imread(path.toStdString(), cv::IMREAD_UNCHANGED));
    if (m.empty()) {   // repli sur les décodeurs Qt (GIF, etc.)
        QImage q(path);
        if (!q.isNull()) m = mu::fromQImage(q);
    }
    if (m.empty()) { if (err) *err = Tr::tr("Format d'image non reconnu ou fichier illisible."); return nullptr; }
    auto* d = new Document(QSize(m.cols, m.rows));
    d->applyStructure({QSize(m.cols, m.rows), {Layer::create(Tr::tr("Arrière-plan"), m)}, 0});
    d->path = path;
    d->undoStack()->clear();
    return d;
}

bool save(Document* d, const QString& path, QString* err, int q) {
    if (isNativeProject(path)) {
        bool ok = saveProject(d, path, err);
        if (ok) { d->path = path; d->undoStack()->setClean(); }
        return ok;
    }
    cv::Mat comp = d->compositeCopy();
    QString ext = QFileInfo(path).suffix().toLower();
    cv::Mat out = comp;
    std::vector<int> params;
    if (ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "ppm" || ext == "pgm") {   // pas d'alpha : aplatir sur blanc
        cv::Mat white = mu::newMat(d->size(), {255, 255, 255, 255});
        std::vector<cv::Mat> ch; cv::Mat a; cv::extractChannel(comp, a, 3);
        cv::Mat bgr; cv::cvtColor(comp, bgr, cv::COLOR_BGRA2BGR);
        cv::Mat f, wf, af; bgr.convertTo(f, CV_32FC3); af = cv::Mat(); a.convertTo(af, CV_32F, 1.0 / 255.0);
        cv::Mat a3; cv::cvtColor(af, a3, cv::COLOR_GRAY2BGR);
        cv::Mat res = f.mul(a3) + cv::Mat(f.size(), CV_32FC3, cv::Scalar::all(255)).mul(cv::Scalar::all(1.0) - a3);
        res.convertTo(out, CV_8UC3);
        params = {cv::IMWRITE_JPEG_QUALITY, q};
    } else if (ext == "webp") params = {cv::IMWRITE_WEBP_QUALITY, q};
    bool ok = false;
    try { ok = cv::imwrite(path.toStdString(), out, params); } catch (const cv::Exception& e) { if (err) *err = e.what(); return false; }
    if (!ok) { if (err) *err = Tr::tr("Écriture impossible (format ou chemin non supporté)."); return false; }
    return true;
}
}
