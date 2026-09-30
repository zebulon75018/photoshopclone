#pragma once
// Génération d'images par Stable Diffusion via stable-diffusion.cpp (https://github.com/leejet/stable-diffusion.cpp) :
// texte -> image, et inpainting (régénération de la zone sélectionnée).
//
// ARCHITECTURE
//  * La bibliothèque est chargée À L'EXÉCUTION (QLibrary/dlopen) depuis « libpcsd.so » : si elle est absente, PhotoClone démarre
//    normalement et les commandes correspondantes l'indiquent. Elle embarque SA PROPRE copie de ggml (version différente de celle de
//    vision.cpp) dont tous les symboles sont masqués (script de version, voir depend/stablediffusioncpp/) : aucun conflit possible.
//  * Chaque génération s'exécute dans un fil dédié (Sd::Job, un QThread) : l'interface reste fluide. Chargement du modèle, échantillonnage
//    et décodage y ont lieu ; la progression, le journal et le résultat reviennent par signaux (file d'attente Qt, donc dans le fil de l'interface).
//  * ANNULATION : Job::cancel() est sûr depuis n'importe quel fil. Il lève un drapeau propre au Job ET appelle sd_cancel_generation().
//    Ce dernier étant remis à zéro au tout début de generate_image(), le drapeau est aussi ré-affirmé à chaque étape de progression et
//    contrôlé avant/après l'appel : une demande d'annulation n'est jamais perdue. Limites (dues à la bibliothèque) : l'arrêt est effectif
//    à la fin de l'étape de calcul en cours (de ~1 s à plusieurs dizaines de s sur CPU), et le CHARGEMENT du modèle ne peut pas être
//    interrompu (l'annulation prend effet dès qu'il est terminé).
//  * Un seul Job à la fois (le contexte de modèle, coûteux en mémoire, est conservé entre deux générations).
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <functional>
#include <opencv2/core.hpp>
#include <vector>

namespace Sd {

// --- Configuration (mémorisée dans les réglages de l'application) --------------------------------------------------------
struct Config {
    QString model;           // checkpoint complet (.safetensors / .ckpt / .gguf) : SD 1.x/2.x/XL… (cas le plus courant)
    QString vae;             // VAE externe optionnel
    QString diffusionModel;  // modèle de diffusion seul (Flux, SD3… : à associer aux encodeurs de texte ci-dessous)
    QString clipL, clipG, t5xxl;
    int threads = 0;         // 0 = automatique (cœurs physiques)
    bool flashAttention = false;
    bool mmap = true;        // projection mémoire des poids (démarrage plus rapide, moins de RAM)
    bool unloadAfterUse = false;   // libère la mémoire du modèle après chaque génération
    bool operator==(const Config&) const = default;
};
Config loadConfig();
void saveConfig(const Config&);
bool hasModel(const Config&);    // au moins un checkpoint ou un modèle de diffusion renseigné

// Vérifie l'existence et l'en-tête d'un fichier de poids (safetensors / GGUF / ckpt-zip / pickle). Chaîne vide = plausible.
// Ne prouve pas que le modèle est compatible : c'est un garde-fou contre les fichiers manifestement invalides.
QString validateModelFile(const QString& path);
// Vérifie toute la configuration ; chaîne vide = prête à l'emploi.
QString validateConfig(const Config&);

// --- Bibliothèque ---------------------------------------------------------------------------------------------------------
bool libraryCompiled();                    // l'en-tête stable-diffusion.h était présent à la compilation (PC_HAVE_SDCPP)
bool libraryLoaded(QString* error = nullptr);   // tente (une fois) de charger libpcsd.so
QString libraryPath();                     // chemin effectivement chargé (vide si aucun)
QString libraryVersionInfo();              // sd_get_system_info() si chargée

// --- Requêtes -------------------------------------------------------------------------------------------------------------
struct Request {
    enum Mode { Txt2Img, Inpaint } mode = Txt2Img;
    QString prompt, negative;
    int width = 512, height = 512;          // multiples de 8 ; Inpaint : taille de l'image de travail (= initImage)
    int steps = 20;
    float cfg = 7.0f;
    qint64 seed = -1;                       // < 0 : aléatoire (la graine effective est renvoyée dans Result::seed)
    QString sampler;                        // vide = automatique (défaut du modèle) ; sinon nom (« euler_a », « dpm++2m »…)
    QString scheduler;                      // vide = automatique
    int clipSkip = -1;                      // -1 = défaut du modèle
    float strength = 0.8f;                  // Inpaint : force de régénération (1 = ignore totalement le contenu d'origine)
    int batch = 1;                          // nombre de variantes (1..8)
    bool vaeTiling = false;                 // découpe le décodage VAE en tuiles (économise la mémoire sur grandes images)
    cv::Mat initImage;                      // Inpaint : BGRA (width x height)
    cv::Mat mask;                           // Inpaint : CV_8UC1 (width x height), 255 = zone à régénérer
    Config config;                          // configuration des modèles utilisée pour ce Job
};

struct Result {
    bool ok = false;
    bool cancelled = false;
    QString error;
    std::vector<cv::Mat> images;            // BGRA (CV_8UC4), width x height, opaques
    qint64 seed = 0;                        // graine de la première variante (les suivantes : seed + i)
    double seconds = 0;
    QString modelVersion;                   // p. ex. « SD 1.x », « SDXL » (d'après la bibliothèque)
    QStringList logTail;                    // dernières lignes du journal (utile au diagnostic en cas d'échec)
};

class Job : public QThread {
    Q_OBJECT
public:
    const Request& request() const { return m_req; }
    // Demande l'arrêt : sûr depuis n'importe quel fil, idempotent, non bloquant.
    void cancel();
    bool cancelRequested() const { return m_cancel.load(); }

    // Émission d'événements (utilisés par le moteur réel ET par les fonctions de test injectées). Appelables depuis le fil de calcul.
    // Ils sont POSTÉS dans la boucle d'événements du fil de l'interface (jamais émis directement) : un appelant qui se connecte juste
    // après Sd::start() ne peut donc rien manquer, même si le calcul échoue instantanément ; l'ordre d'émission est conservé.
    void reportStage(const QString& s) { post([this, s] { emit stageChanged(s); }); }
    void reportProgress(int step, int steps, double secPerStep) { post([this, step, steps, secPerStep] { emit progress(step, steps, secPerStep); }); }
    void reportLog(int level, const QString& s) { post([this, level, s] { emit logLine(level, s); }); }
    void reportResult(const Result& r) { post([this, r] { emit resultReady(r); }); }

signals:
    void stageChanged(const QString& text);            // « Chargement du modèle… », « Génération… », « Image 2/4 »
    void progress(int step, int steps, double secPerStep);
    void logLine(int level, const QString& text);      // niveaux : 0 debug … 2 info, 3 avertissement, 4 erreur
    void resultReady(const Sd::Result& result);        // toujours émis exactement une fois, même en cas d'annulation ou d'échec

protected:
    void run() override;

private:
    void post(std::function<void()> fn);
    friend Job* start(const Request&, QString*);
    Request m_req;
    std::atomic<bool> m_cancel{false};
};

// Lance un Job dans son propre fil. Retourne nullptr (et remplit *whyNot) si la bibliothèque est indisponible, si la configuration
// est invalide ou si un autre Job est encore actif. Le Job est détruit automatiquement à la fin de son fil ; l'appelant peut s'y connecter.
Job* start(const Request& request, QString* whyNot = nullptr);
bool isBusy();
// Libère le modèle chargé (mémoire). Sans effet si un Job est actif.
void unloadModel();
// Annule le Job actif, attend sa fin, libère le modèle. À appeler avant de quitter l'application.
void shutdown();

// Point d'injection pour les tests : remplace le moteur réel par une fonction (même contrat : appeler job.cancelRequested(), reportProgress()…).
using TestEngine = std::function<Result(const Request&, Job&)>;
void setTestEngine(TestEngine engine);

}
Q_DECLARE_METATYPE(Sd::Result)
