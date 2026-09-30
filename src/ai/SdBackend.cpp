#include "SdBackend.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <mutex>
#include <opencv2/imgproc.hpp>

#ifdef PC_HAVE_SDCPP
#include <QLibrary>
#include <cstdlib>
#include "stable-diffusion.h"
#endif

namespace Sd {

// ============================================================================================ configuration
static QSettings settings() { return QSettings("PhotoClone", "PhotoClone"); }

Config loadConfig() {
    QSettings s = settings();
    s.beginGroup("sd");
    Config c;
    c.model = s.value("model").toString();
    c.vae = s.value("vae").toString();
    c.diffusionModel = s.value("diffusionModel").toString();
    c.clipL = s.value("clipL").toString();
    c.clipG = s.value("clipG").toString();
    c.t5xxl = s.value("t5xxl").toString();
    c.threads = s.value("threads", 0).toInt();
    c.flashAttention = s.value("flashAttention", false).toBool();
    c.mmap = s.value("mmap", true).toBool();
    c.unloadAfterUse = s.value("unloadAfterUse", false).toBool();
    return c;
}

void saveConfig(const Config& c) {
    QSettings s = settings();
    s.beginGroup("sd");
    auto put = [&](const char* k, const QString& v) { if (v.isEmpty()) s.remove(k); else s.setValue(k, v); };
    put("model", c.model); put("vae", c.vae); put("diffusionModel", c.diffusionModel);
    put("clipL", c.clipL); put("clipG", c.clipG); put("t5xxl", c.t5xxl);
    s.setValue("threads", c.threads);
    s.setValue("flashAttention", c.flashAttention);
    s.setValue("mmap", c.mmap);
    s.setValue("unloadAfterUse", c.unloadAfterUse);
}

bool hasModel(const Config& c) { return !c.model.trimmed().isEmpty() || !c.diffusionModel.trimmed().isEmpty(); }

QString validateModelFile(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return "fichier introuvable : " + path;
    if (!fi.isReadable()) return "fichier illisible (droits) : " + path;
    if (fi.size() < 16) return "fichier vide ou trop petit pour être un modèle.";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return "impossible d'ouvrir le fichier.";
    const QByteArray head = f.read(16);
    if (head.size() < 16) return "fichier illisible.";
    if (head.startsWith("GGUF")) return {};                                     // GGUF
    if (head.startsWith("PK\x03\x04")) return {};                               // ckpt / pt (archive zip PyTorch)
    if (static_cast<uchar>(head[0]) == 0x80) return {};                         // pickle brut (anciens .ckpt)
    quint64 n = 0;                                                             // safetensors : u64 LE = taille de l'en-tête JSON
    for (int i = 7; i >= 0; --i) n = (n << 8) | static_cast<uchar>(head[i]);
    if (n > 0 && n < (256ull << 20) && n + 8 <= static_cast<quint64>(fi.size()) && head[8] == '{') return {};
    return "format non reconnu (attendu : .safetensors, .gguf ou .ckpt).";
}

QString validateConfig(const Config& c) {
    if (!hasModel(c)) return "aucun modèle configuré (menu IA > Réglages de Stable Diffusion…).";
    struct F { const char* label; const QString* path; };
    for (const F& f : {F{"Checkpoint", &c.model}, F{"VAE", &c.vae}, F{"Modèle de diffusion", &c.diffusionModel},
                       F{"CLIP-L", &c.clipL}, F{"CLIP-G", &c.clipG}, F{"T5-XXL", &c.t5xxl}})
        if (!f.path->trimmed().isEmpty())
            if (QString bad = validateModelFile(*f.path); !bad.isEmpty()) return QString("%1 : %2").arg(f.label, bad);
    return {};
}

// ============================================================================================ état global du moteur
namespace {
std::mutex g_mtx;                 // protège g_ctx / g_ctxKey (jamais tenu pendant un calcul long)
QPointer<Job> g_job;              // fil de l'interface uniquement
std::atomic<Job*> g_activeJob{nullptr};
TestEngine g_test;
std::mutex g_logMtx;
QStringList g_logRing;

[[maybe_unused]] void pushLog(const QString& s) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    g_logRing << s;
    while (g_logRing.size() > 200) g_logRing.removeFirst();
}
QStringList logTail(int n = 12) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    return g_logRing.mid(std::max(0, int(g_logRing.size()) - n));
}
[[maybe_unused]] QString lastErrorLines() {
    std::lock_guard<std::mutex> lk(g_logMtx);
    QStringList errs;
    for (const QString& l : g_logRing) if (l.startsWith("[ERREUR]") || l.startsWith("[ATTENTION]")) errs << l;
    return errs.mid(std::max(0, int(errs.size()) - 3)).join("\n");
}
}

void setTestEngine(TestEngine e) { g_test = std::move(e); }

#ifdef PC_HAVE_SDCPP
// ============================================================================================ chargement dynamique de la bibliothèque
namespace {
struct Api {
    bool tried = false, ok = false;
    QString error, path;
    QLibrary* lib = nullptr;
    decltype(&::sd_set_log_callback) setLog = nullptr;
    decltype(&::sd_set_progress_callback) setProgress = nullptr;
    decltype(&::sd_get_num_physical_cores) numCores = nullptr;
    decltype(&::sd_get_system_info) systemInfo = nullptr;
    decltype(&::sd_ctx_params_init) ctxParamsInit = nullptr;
    decltype(&::new_sd_ctx) newCtx = nullptr;
    decltype(&::free_sd_ctx) freeCtx = nullptr;
    decltype(&::sd_ctx_supports_image_generation) supportsImage = nullptr;
    decltype(&::sd_get_model_version_name) versionName = nullptr;
    decltype(&::sd_img_gen_params_init) imgParamsInit = nullptr;
    decltype(&::generate_image) generate = nullptr;
    decltype(&::free_sd_images) freeImages = nullptr;
    decltype(&::sd_cancel_generation) cancel = nullptr;
    decltype(&::str_to_sample_method) strToSampler = nullptr;
    decltype(&::str_to_scheduler) strToScheduler = nullptr;
};
Api g_api;
std::mutex g_apiMtx;
sd_ctx_t* g_ctx = nullptr;
QString g_ctxKey;

void logCb(sd_log_level_t level, const char* text, void*) {
    if (level < SD_LOG_INFO || !text) return;
    static const char* names[] = {"DEBUG", "VERBOSE", "INFO", "ATTENTION", "ERREUR"};
    QString msg = QString::fromUtf8(text).trimmed();
    if (msg.isEmpty()) return;
    const QString line = QString("[%1] %2").arg(names[std::clamp(int(level), 0, 4)], msg);
    pushLog(line);
    if (Job* j = g_activeJob.load()) {
        j->reportLog(int(level), msg);
        static const QRegularExpression rx("generating image:\\s*(\\d+)/(\\d+)");   // « generating image: 2/4 - seed … »
        if (auto m = rx.match(msg); m.hasMatch()) j->reportStage(QString("Image %1 sur %2").arg(m.captured(1), m.captured(2)));
    }
}

void progressCb(int step, int steps, float time, void*) {
    Job* j = g_activeJob.load();
    if (!j) return;
    if (j->cancelRequested()) j->cancel();      // ré-affirme l'annulation (generate_image() remet le drapeau interne à zéro à son démarrage)
    j->reportProgress(step, steps, double(time));
}

QStringList libraryCandidates() {
    QStringList c;
    const QByteArray env = qgetenv("PHOTOCLONE_SD_LIB");
    if (!env.isEmpty()) c << QString::fromLocal8Bit(env);
    const QString app = QCoreApplication::applicationDirPath();
    c << app + "/libpcsd.so" << app + "/../lib/libpcsd.so" << app + "/lib/libpcsd.so";
#ifdef PC_SD_LIB_PATH
    c << QString(PC_SD_LIB_PATH);
#endif
    c << "libpcsd.so";
    return c;
}

bool ensureLoaded(QString* error) {
    std::lock_guard<std::mutex> lk(g_apiMtx);
    if (!g_api.tried) {
        g_api.tried = true;
        QStringList problems;
        for (const QString& cand : libraryCandidates()) {
            if (QFileInfo(cand).isAbsolute() && !QFileInfo::exists(cand)) continue;
            auto* lib = new QLibrary(cand);
            lib->setLoadHints(QLibrary::ResolveAllSymbolsHint);      // dlopen(RTLD_NOW | RTLD_LOCAL) : rien n'est exporté globalement
            if (!lib->load()) { problems << cand + " : " + lib->errorString(); delete lib; continue; }
            bool all = true;
            QStringList missing;
            auto res = [&](auto& fn, const char* name) {
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(lib->resolve(name));
                if (!fn) { all = false; missing << name; }
            };
            Api a;
            res(a.setLog, "sd_set_log_callback"); res(a.setProgress, "sd_set_progress_callback");
            res(a.numCores, "sd_get_num_physical_cores"); res(a.systemInfo, "sd_get_system_info");
            res(a.ctxParamsInit, "sd_ctx_params_init"); res(a.newCtx, "new_sd_ctx"); res(a.freeCtx, "free_sd_ctx");
            res(a.supportsImage, "sd_ctx_supports_image_generation"); res(a.versionName, "sd_get_model_version_name");
            res(a.imgParamsInit, "sd_img_gen_params_init"); res(a.generate, "generate_image"); res(a.freeImages, "free_sd_images");
            res(a.cancel, "sd_cancel_generation"); res(a.strToSampler, "str_to_sample_method"); res(a.strToScheduler, "str_to_scheduler");
            if (!all) { problems << cand + " : symboles manquants (" + missing.join(", ") + ") — bibliothèque incompatible ?"; lib->unload(); delete lib; continue; }
            a.tried = true; a.ok = true; a.lib = lib; a.path = cand;
            g_api = a;
            g_api.setLog(logCb, nullptr);
            g_api.setProgress(progressCb, nullptr);
            break;
        }
        if (!g_api.ok) g_api.error = problems.isEmpty()
            ? "libpcsd.so introuvable (voir depend/stablediffusioncpp/BUILD_FROM_SOURCE.md)."
            : "impossible de charger libpcsd.so : " + problems.join(" ; ");
    }
    if (!g_api.ok && error) *error = g_api.error;
    return g_api.ok;
}

QString configKey(const Config& c) {
    return QStringList{c.model, c.vae, c.diffusionModel, c.clipL, c.clipG, c.t5xxl, QString::number(c.threads),
                       c.flashAttention ? "fa" : "", c.mmap ? "mm" : ""}.join('|');
}

void freeContextLocked() {
    sd_ctx_t* old = nullptr;
    { std::lock_guard<std::mutex> lk(g_mtx); old = g_ctx; g_ctx = nullptr; g_ctxKey.clear(); }
    if (old) g_api.freeCtx(old);
}

sd_ctx_t* acquireContext(const Config& cfg, QString* error) {
    const QString key = configKey(cfg);
    { std::lock_guard<std::mutex> lk(g_mtx); if (g_ctx && g_ctxKey == key) return g_ctx; }
    freeContextLocked();     // configuration modifiée : on libère l'ancien modèle AVANT d'en charger un autre (mémoire)

    sd_ctx_params_t p;
    g_api.ctxParamsInit(&p);
    QByteArray bModel = cfg.model.toUtf8(), bVae = cfg.vae.toUtf8(), bDiff = cfg.diffusionModel.toUtf8(),
               bL = cfg.clipL.toUtf8(), bG = cfg.clipG.toUtf8(), bT5 = cfg.t5xxl.toUtf8();
    if (!cfg.model.isEmpty()) p.model_path = bModel.constData();
    if (!cfg.vae.isEmpty()) p.vae_path = bVae.constData();
    if (!cfg.diffusionModel.isEmpty()) p.diffusion_model_path = bDiff.constData();
    if (!cfg.clipL.isEmpty()) p.clip_l_path = bL.constData();
    if (!cfg.clipG.isEmpty()) p.clip_g_path = bG.constData();
    if (!cfg.t5xxl.isEmpty()) p.t5xxl_path = bT5.constData();
    p.n_threads = cfg.threads > 0 ? cfg.threads : std::max(1, int(g_api.numCores()));
    p.flash_attn = cfg.flashAttention;
    p.diffusion_flash_attn = cfg.flashAttention;
    p.enable_mmap = cfg.mmap;

    sd_ctx_t* ctx = g_api.newCtx(&p);
    if (!ctx) {
        if (error) {
            *error = "Impossible de charger le modèle (fichier incompatible, corrompu ou mémoire insuffisante).";
            if (QString e = lastErrorLines(); !e.isEmpty()) *error += "\n" + e;
        }
        return nullptr;
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ctx = ctx;
    g_ctxKey = key;
    return ctx;
}

Result runReal(Job& job) {
    Result r;
    const Request& q = job.request();
    QString err;
    if (!ensureLoaded(&err)) { r.error = err; return r; }
    if (QString bad = validateConfig(q.config); !bad.isEmpty()) { r.error = bad; return r; }

    job.reportStage("Chargement du modèle (peut être long la première fois)…");
    sd_ctx_t* ctx = acquireContext(q.config, &err);
    if (!ctx) { r.error = err; return r; }
    if (job.cancelRequested()) { r.cancelled = true; return r; }
    if (!g_api.supportsImage(ctx)) { r.error = "Ce modèle ne prend pas en charge la génération d'images."; return r; }
    r.modelVersion = QString::fromUtf8(g_api.versionName(ctx));

    sd_img_gen_params_t gp;
    g_api.imgParamsInit(&gp);
    const QByteArray prompt = q.prompt.toUtf8(), negative = q.negative.toUtf8();
    gp.prompt = prompt.constData();
    gp.negative_prompt = negative.constData();
    gp.width = q.width;
    gp.height = q.height;
    gp.clip_skip = q.clipSkip;
    gp.strength = q.strength;
    gp.batch_count = q.batch;
    gp.sample_params.sample_steps = q.steps;
    gp.sample_params.guidance.txt_cfg = q.cfg;
    const qint64 seed = q.seed >= 0 ? q.seed : qint64(QRandomGenerator::global()->bounded(quint32(0x7fffffff)));
    gp.seed = seed;
    if (!q.sampler.isEmpty()) {
        gp.sample_params.sample_method = g_api.strToSampler(q.sampler.toUtf8().constData());
        if (gp.sample_params.sample_method == SAMPLE_METHOD_COUNT) { r.error = "Échantillonneur inconnu : " + q.sampler; return r; }
    }
    if (!q.scheduler.isEmpty()) {
        gp.sample_params.scheduler = g_api.strToScheduler(q.scheduler.toUtf8().constData());
        if (gp.sample_params.scheduler == SCHEDULER_COUNT) { r.error = "Ordonnanceur inconnu : " + q.scheduler; return r; }
    }
    if (q.vaeTiling) gp.vae_tiling_params.enabled = true;

    cv::Mat rgb, mask;
    if (q.mode == Request::Inpaint) {       // les tampons doivent rester en vie pendant generate_image()
        cv::cvtColor(q.initImage, rgb, cv::COLOR_BGRA2RGB);
        mask = q.mask.isContinuous() ? q.mask : q.mask.clone();
        gp.init_image = sd_image_t{uint32_t(q.width), uint32_t(q.height), 3, rgb.data};
        gp.mask_image = sd_image_t{uint32_t(q.width), uint32_t(q.height), 1, mask.data};
    }

    if (job.cancelRequested()) { r.cancelled = true; return r; }
    job.reportStage(q.mode == Request::Inpaint ? "Inpainting…" : "Génération…");
    sd_image_t* imgs = nullptr;
    int n = 0;
    const bool ok = g_api.generate(ctx, &gp, &imgs, &n);
    if (!ok || !imgs || n <= 0) {
        if (imgs) g_api.freeImages(imgs, n);
        if (job.cancelRequested()) { r.cancelled = true; return r; }
        r.error = "La génération a échoué.";
        if (QString e = lastErrorLines(); !e.isEmpty()) r.error += "\n" + e;
        return r;
    }
    for (int i = 0; i < n; ++i) {
        const sd_image_t& im = imgs[i];
        if (!im.data || im.width == 0 || im.height == 0 || (im.channel != 3 && im.channel != 4)) continue;
        cv::Mat src(int(im.height), int(im.width), im.channel == 3 ? CV_8UC3 : CV_8UC4, im.data), out;
        cv::cvtColor(src, out, im.channel == 3 ? cv::COLOR_RGB2BGRA : cv::COLOR_RGBA2BGRA);
        cv::Mat opaque = out;
        cv::insertChannel(cv::Mat(out.size(), CV_8UC1, cv::Scalar(255)), opaque, 3);
        r.images.push_back(opaque);
    }
    g_api.freeImages(imgs, n);
    if (job.cancelRequested()) { r.images.clear(); r.cancelled = true; return r; }
    if (r.images.empty()) { r.error = "Le modèle n'a renvoyé aucune image exploitable."; return r; }
    r.ok = true;
    r.seed = seed;
    return r;
}
}   // namespace

bool libraryCompiled() { return true; }
bool libraryLoaded(QString* error) { return ensureLoaded(error); }
QString libraryPath() { std::lock_guard<std::mutex> lk(g_apiMtx); return g_api.ok ? g_api.path : QString(); }
QString libraryVersionInfo() { return ensureLoaded(nullptr) ? QString::fromUtf8(g_api.systemInfo()) : QString(); }

void Job::cancel() {
    m_cancel = true;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_ctx && g_api.ok && g_activeJob.load() == this) g_api.cancel(g_ctx, SD_CANCEL_ALL);
}

void unloadModel() {
    if (g_job && g_job->isRunning()) return;
    if (g_api.ok) freeContextLocked();
}
#else
// ============================================================================================ sans stable-diffusion.cpp
namespace {
Result runReal(Job&) {
    Result r;
    r.error = "stable-diffusion.cpp n'a pas été compilé dans cette version de PhotoClone (voir depend/stablediffusioncpp/BUILD_FROM_SOURCE.md).";
    return r;
}
}
bool libraryCompiled() { return false; }
bool libraryLoaded(QString* error) {
    if (error) *error = "stable-diffusion.cpp n'a pas été compilé dans cette version de PhotoClone.";
    return false;
}
QString libraryPath() { return {}; }
QString libraryVersionInfo() { return {}; }
void Job::cancel() { m_cancel = true; }
void unloadModel() {}
#endif

// ============================================================================================ Job
void Job::run() {
    QElapsedTimer t;
    t.start();
    Result res;
    {
        std::lock_guard<std::mutex> lk(g_logMtx);
        g_logRing.clear();
    }
    g_activeJob = this;
    try {
        res = g_test ? g_test(m_req, *this) : runReal(*this);
    } catch (const std::exception& e) {
        res = Result{};
        res.error = QString("Erreur interne : ") + e.what();
    } catch (...) {
        res = Result{};
        res.error = "Erreur interne inconnue.";
    }
    g_activeJob = nullptr;
    if (m_cancel.load()) {          // annuler = abandonner : aucun résultat partiel n'est livré
        res.ok = false;
        res.cancelled = true;
        res.images.clear();
        res.error.clear();
    }
    res.seconds = double(t.elapsed()) / 1000.0;
    res.logTail = logTail();
#ifdef PC_HAVE_SDCPP
    if (!g_test && m_req.config.unloadAfterUse) freeContextLocked();
#endif
    reportResult(res);
}

void Job::post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);   // exécuté dans le fil de l'objet (interface)
}

Job* start(const Request& r, QString* why) {
    static const bool registered = (qRegisterMetaType<Sd::Result>("Sd::Result"), true);
    Q_UNUSED(registered);
    auto fail = [&](const QString& m) -> Job* { if (why) *why = m; return nullptr; };
    if (g_job && g_job->isRunning() && !g_job->wait(1500)) return fail("Une génération est déjà en cours.");
    if (!g_test) {
        QString e;
        if (!libraryCompiled()) return fail("stable-diffusion.cpp n'a pas été compilé dans cette version de PhotoClone.");
        if (!libraryLoaded(&e)) return fail(e);
        if (QString bad = validateConfig(r.config); !bad.isEmpty()) return fail(bad);
    }
    if (r.width < 64 || r.height < 64 || r.width > 4096 || r.height > 4096 || r.width % 8 || r.height % 8)
        return fail(QString("Taille invalide (%1 × %2) : multiples de 8 entre 64 et 4096 requis.").arg(r.width).arg(r.height));
    if (r.steps < 1 || r.steps > 200) return fail("Nombre d'étapes invalide (1 à 200).");
    if (r.batch < 1 || r.batch > 8) return fail("Nombre de variantes invalide (1 à 8).");
    if (r.mode == Request::Inpaint) {
        if (r.initImage.type() != CV_8UC4 || r.mask.type() != CV_8UC1 || r.initImage.cols != r.width || r.initImage.rows != r.height ||
            r.mask.size() != r.initImage.size())
            return fail("Inpainting : image (BGRA) et masque (8 bits) doivent avoir la taille de travail.");
        if (cv::countNonZero(r.mask) == 0) return fail("Inpainting : le masque est vide.");
    }
    auto* j = new Job;
    j->m_req = r;
    QObject::connect(j, &QThread::finished, j, &QObject::deleteLater);
    g_job = j;
    j->QThread::start();
    return j;
}

bool isBusy() { return g_job && g_job->isRunning(); }

void shutdown() {
    if (g_job) {
        g_job->cancel();
        g_job->wait();
    }
    unloadModel();
}

}   // namespace Sd
