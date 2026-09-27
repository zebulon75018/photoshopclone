#include "ImageIO.h"
#include "Document.h"
#include "MatUtil.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>

namespace ImageIO {
static const quint32 MAGIC = 0x50434C31;   // "PCL1"

QString openFilter() {
    return "Toutes les images (*.pcl *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.ppm *.pgm *.gif);;Projet PhotoClone (*.pcl);;Tous les fichiers (*)";
}
QString saveFilter() {
    return "Projet PhotoClone (*.pcl);;PNG (*.png);;JPEG (*.jpg *.jpeg);;TIFF (*.tif *.tiff);;WebP (*.webp);;BMP (*.bmp)";
}
bool isNativeProject(const QString& p) { return p.endsWith(".pcl", Qt::CaseInsensitive); }

static QByteArray encodePng(const cv::Mat& m) {
    std::vector<uchar> buf;
    cv::imencode(".png", m, buf, {cv::IMWRITE_PNG_COMPRESSION, 3});
    return QByteArray(reinterpret_cast<const char*>(buf.data()), int(buf.size()));
}
static cv::Mat decodePng(const QByteArray& b, int flags) {
    if (b.isEmpty()) return cv::Mat();
    std::vector<uchar> v(b.begin(), b.end());
    return cv::imdecode(v, flags);
}

static bool saveProject(Document* d, const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = f.errorString(); return false; }
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    s << MAGIC << qint32(1) << qint32(d->size().width()) << qint32(d->size().height()) << qint32(d->activeIndex()) << qint32(d->layers().size());
    for (auto& l : d->layers()) {
        s << l->props.name << l->props.visible << l->props.locked << l->props.opacity << qint32(l->props.blend) << l->props.maskEnabled;
        s << encodePng(l->image) << (l->hasMask() ? encodePng(l->mask) : QByteArray());
        s << l->isText();
        if (l->isText()) {
            const TextData& t = *l->text;
            s << t.text << t.family << qint32(t.pixelSize) << t.bold << t.italic << t.underline << t.color << qint32(t.align) << t.pos;
        }
    }
    return s.status() == QDataStream::Ok;
}

static Document* loadProject(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); return nullptr; }
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    quint32 magic; qint32 ver, w, h, active, n;
    s >> magic >> ver >> w >> h >> active >> n;
    if (magic != MAGIC || w <= 0 || h <= 0 || n < 0) { if (err) *err = "Fichier de projet invalide."; return nullptr; }
    std::unique_ptr<Document> d(new Document(QSize(w, h)));
    Document::Structure st{QSize(w, h), {}, active};
    for (int i = 0; i < n; ++i) {
        LayerProps p; qint32 blend; QByteArray img, msk; bool isText;
        s >> p.name >> p.visible >> p.locked >> p.opacity >> blend >> p.maskEnabled >> img >> msk >> isText;
        p.blend = BlendMode(blend);
        cv::Mat m = decodePng(img, cv::IMREAD_UNCHANGED);
        if (m.empty() || m.channels() != 4 || m.size() != cv::Size(w, h)) { if (err) *err = "Calque corrompu."; return nullptr; }
        auto l = Layer::create(p.name, m);
        l->props = p;
        l->mask = decodePng(msk, cv::IMREAD_GRAYSCALE);
        if (isText) {
            TextData t; qint32 px, al;
            s >> t.text >> t.family >> px >> t.bold >> t.italic >> t.underline >> t.color >> al >> t.pos;
            t.pixelSize = px; t.align = al;
            l->text = t;
        }
        st.layers.push_back(l);
    }
    if (s.status() != QDataStream::Ok) { if (err) *err = "Lecture incomplète."; return nullptr; }
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
    if (m.empty()) { if (err) *err = "Format d'image non reconnu ou fichier illisible."; return nullptr; }
    auto* d = new Document(QSize(m.cols, m.rows));
    d->applyStructure({QSize(m.cols, m.rows), {Layer::create("Arrière-plan", m)}, 0});
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
    if (!ok) { if (err) *err = "Écriture impossible (format ou chemin non supporté)."; return false; }
    return true;
}
}
