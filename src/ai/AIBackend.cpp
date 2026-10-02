#include "AIBackend.h"
#include "AIModels.h"
#include <QCoreApplication>
#include <QFileInfo>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(AIBackend) }; }   // traductions hors classes QObject (voir translations/)

#ifdef PC_HAVE_VISIONCPP
#include "VispBridge.h"
#include <visp/vision.h>
#include <gguf.h>
#include <optional>
#include <QFileInfo>
#include <QCoreApplication>

namespace AIBackend {
using namespace visp;

namespace {
const char* expectedArch(AIArchitecture a) {
    switch (a) {
    case AIArchitecture::Sam: return "mobile-sam";
    case AIArchitecture::BiRefNet: return "birefnet";
    case AIArchitecture::DepthAnything: return "depthanything";
    case AIArchitecture::MiGan: return "migan";
    case AIArchitecture::Esrgan: return "esrgan";
    }
    return "";
}

backend_device& backend() {
    static backend_device b = visp::backend_init();
    return b;
}

// Charge (ou recharge si le chemin configuré a changé) un modèle et le met en cache dans `slot`.
template <class Model, class Loader>
Model* ensureLoaded(std::optional<Model>& slot, QString& loadedPath, AIArchitecture arch, Loader load, QString* error) {
    QString path = AIModels::modelPath(arch);
    if (path.isEmpty()) {
        if (error) *error = Tr::tr("%1 : aucun modèle configuré (menu IA > Réglages des modèles…).").arg(AIModels::info(arch).name);
        return nullptr;
    }
    if (slot && loadedPath == path) return &*slot;
    if (QString bad = validateModelFile(arch, path); !bad.isEmpty()) {
        if (error) *error = Tr::tr("%1 : %2").arg(AIModels::info(arch).name, bad);
        return nullptr;
    }
    try {
        slot.emplace(load(path.toUtf8().constData(), backend()));
        loadedPath = path;
        return &*slot;
    } catch (std::exception const& e) {
        slot.reset();
        loadedPath.clear();
        if (error) *error = Tr::tr("%1 : %2").arg(AIModels::info(arch).name, QString::fromUtf8(e.what()));
        return nullptr;
    }
}

std::optional<birefnet_model> g_birefnet;
QString g_birefnetPath;
std::optional<depthany_model> g_depth;
QString g_depthPath;
std::optional<migan_model> g_migan;
QString g_miganPath;
std::optional<esrgan_model> g_esrgan;
QString g_esrganPath;
std::optional<sam_model> g_sam;
QString g_samPath;
bool g_samEncoded = false;
}

bool available() { return true; }

QString validateModelFile(AIArchitecture arch, const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return Tr::tr("fichier introuvable : %1").arg(path);
    if (!fi.isReadable()) return Tr::tr("fichier illisible (droits) : %1").arg(path);
    gguf_init_params p{};
    p.no_alloc = true;   // lit uniquement les métadonnées, pas les poids
    p.ctx = nullptr;
    gguf_context* g = gguf_init_from_file(path.toUtf8().constData(), p);
    if (!g) return Tr::tr("ce fichier n'est pas un modèle GGUF valide.");
    QString found;
    int64_t k = gguf_find_key(g, "general.architecture");
    if (k >= 0 && gguf_get_kv_type(g, k) == GGUF_TYPE_STRING) found = QString::fromUtf8(gguf_get_val_str(g, k));
    gguf_free(g);
    if (found.isEmpty()) return Tr::tr("fichier GGUF sans architecture déclarée (ce n'est pas un modèle vision.cpp).");
    if (found != expectedArch(arch)) {
        for (const auto& i : AIModels::all())
            if (found == expectedArch(i.id)) return Tr::tr("ce fichier est un modèle %1, pas un modèle %2.").arg(i.name, AIModels::info(arch).name);
        return Tr::tr("architecture « %1 » non prise en charge (attendu : %2).").arg(found, expectedArch(arch));
    }
    return {};
}

Result estimateForeground(const cv::Mat& bgra, const cv::Mat& mask, int radius) {
    Result r;
    if (bgra.type() != CV_8UC4 || mask.type() != CV_8UC1 || bgra.size() != mask.size()) { r.error = Tr::tr("estimateForeground : image BGRA et masque de même taille attendus."); return r; }
    try {
        // La bibliothèque exige des entrées FLOTTANTES (rgba_f32 + alpha_f32) : un masque/une image 8 bits seraient lus
        // comme des flottants (valeurs aberrantes, voire lecture hors tampon). Conversion explicite, RGBA réordonné.
        image_data imgF = image_u8_to_f32(ai::viewOf(bgra), image_format::rgba_f32);
        image_data maskF = image_u8_to_f32(ai::viewOf(mask), image_format::alpha_f32);
        image_data fg = image_estimate_foreground(imgF, maskF, std::max(1, radius));
        image_data u8 = image_f32_to_u8(fg, image_format::rgba_u8);
        r.data = ai::toMatBGRA(u8);
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("Estimation du premier plan : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

Result segmentDichotomous(const cv::Mat& bgra) {
    Result r;
    QString err;
    auto* m = ensureLoaded(g_birefnet, g_birefnetPath, AIArchitecture::BiRefNet, birefnet_load_model, &err);
    if (!m) { r.error = err; return r; }
    try {
        image_data mask = birefnet_compute(*m, ai::viewOf(bgra));
        r.data = ai::toMatGray(mask);
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("BiRefNet : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

bool samEncode(const cv::Mat& bgra, QString* error) {
    QString err;
    auto* m = ensureLoaded(g_sam, g_samPath, AIArchitecture::Sam, sam_load_model, &err);
    if (!m) {
        if (error) *error = err;
        g_samEncoded = false;
        return false;
    }
    try {
        sam_encode(*m, ai::viewOf(bgra));
        g_samEncoded = true;
        return true;
    } catch (std::exception const& e) {
        if (error) *error = Tr::tr("MobileSAM : %1").arg(QString::fromUtf8(e.what()));
        g_samEncoded = false;
        return false;
    }
}

bool samReady() { return g_samEncoded && g_sam.has_value(); }

Result samComputePoint(QPoint p) {
    Result r;
    if (!samReady()) { r.error = Tr::tr("MobileSAM : encodez d'abord l'image (activez l'outil, ou \"Ré-analyser l'image\")."); return r; }
    try {
        image_data mask = sam_compute(*g_sam, i32x2{p.x(), p.y()});
        r.data = ai::toMatGray(mask);
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("MobileSAM : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

Result samComputeBox(QRect box) {
    Result r;
    if (!samReady()) { r.error = Tr::tr("MobileSAM : encodez d'abord l'image (activez l'outil, ou \"Ré-analyser l'image\")."); return r; }
    try {
        box_2d b{i32x2{box.left(), box.top()}, i32x2{box.right(), box.bottom()}};
        image_data mask = sam_compute(*g_sam, b);
        r.data = ai::toMatGray(mask);
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("MobileSAM : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

Result estimateDepth(const cv::Mat& bgra) {
    Result r;
    QString err;
    auto* m = ensureLoaded(g_depth, g_depthPath, AIArchitecture::DepthAnything, depthany_load_model, &err);
    if (!m) { r.error = err; return r; }
    try {
        image_data depth = depthany_compute(*m, ai::viewOf(bgra));
        r.data = ai::toMatGray(depth);
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("Depth-Anything : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

Result inpaint(const cv::Mat& bgra, const cv::Mat& mask) {
    Result r;
    QString err;
    auto* m = ensureLoaded(g_migan, g_miganPath, AIArchitecture::MiGan, migan_load_model, &err);
    if (!m) { r.error = err; return r; }
    try {
        m->params.invert_mask = true;   // convention de ce projet : masque non nul = zone à reconstruire
        image_data out = migan_compute(*m, ai::viewOf(bgra), ai::viewOf(mask));
        r.data = ai::toMatBGRA(out);
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("MI-GAN : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

Result upscale(const cv::Mat& bgra) {
    Result r;
    QString err;
    auto* m = ensureLoaded(g_esrgan, g_esrganPath, AIArchitecture::Esrgan, esrgan_load_model, &err);
    if (!m) { r.error = err; return r; }
    try {
        image_data out = esrgan_compute(*m, ai::viewOf(bgra));
        r.data = ai::toMatBGRA(out);
        r.scale = m->params.scale;
        r.ok = true;
    } catch (std::exception const& e) {
        r.error = Tr::tr("Real-ESRGAN : %1").arg(QString::fromUtf8(e.what()));
    }
    return r;
}

void clearCache() {
    g_birefnet.reset(); g_birefnetPath.clear();
    g_depth.reset(); g_depthPath.clear();
    g_migan.reset(); g_miganPath.clear();
    g_esrgan.reset(); g_esrganPath.clear();
    g_sam.reset(); g_samPath.clear(); g_samEncoded = false;
}

}
#else
namespace AIBackend {
static Result unavailable(const QString& fn) {
    Result r;
    r.error = Tr::tr("La bibliothèque vision.cpp n'a pas été compilée dans cette version de PhotoClone "
                     "(voir depend/visioncpp/BUILD_FROM_SOURCE.md). Fonction demandée : %1").arg(fn);
    return r;
}
bool available() { return false; }
Result segmentDichotomous(const cv::Mat&) { return unavailable("BiRefNet"); }
bool samEncode(const cv::Mat&, QString* error) { if (error) *error = Tr::tr("MobileSAM : la bibliothèque vision.cpp n'a pas été compilée dans cette version."); return false; }
bool samReady() { return false; }
Result samComputePoint(QPoint) { return unavailable("MobileSAM"); }
Result samComputeBox(QRect) { return unavailable("MobileSAM"); }
Result estimateDepth(const cv::Mat&) { return unavailable("Depth-Anything"); }
Result inpaint(const cv::Mat&, const cv::Mat&) { return unavailable("MI-GAN"); }
Result upscale(const cv::Mat&) { return unavailable("Real-ESRGAN"); }
Result estimateForeground(const cv::Mat&, const cv::Mat&, int) { return unavailable(Tr::tr("estimation du premier plan")); }
QString validateModelFile(AIArchitecture, const QString& path) {
    return QFileInfo(path).isFile() ? QString() : Tr::tr("fichier introuvable : %1").arg(path);
}
void clearCache() {}
}
#endif
