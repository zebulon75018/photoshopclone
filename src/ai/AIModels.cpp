#include "AIModels.h"
#include <QSettings>
#include <QCoreApplication>

namespace { struct Tr { Q_DECLARE_TR_FUNCTIONS(AIModels) }; }   // traductions hors classes QObject (voir translations/)

namespace AIModels {

const std::vector<AIArchitectureInfo>& all() {
    static const std::vector<AIArchitectureInfo> v = {
        {AIArchitecture::Sam, "sam", "MobileSAM", Tr::tr("Segmentation par indication (point / boîte)"),
         "https://huggingface.co/Acly/MobileSAM-GGUF/tree/main", "MobileSAM-F16.gguf"},
        {AIArchitecture::BiRefNet, "birefnet", "BiRefNet", Tr::tr("Segmentation dichotomique (détourage automatique)"),
         "https://huggingface.co/Acly/BiRefNet-GGUF/tree/main", "BiRefNet-lite-F16.gguf"},
        {AIArchitecture::DepthAnything, "depthany", "Depth-Anything V2", Tr::tr("Estimation de profondeur"),
         "https://huggingface.co/Acly/Depth-Anything-V2-GGUF/tree/main", "Depth-Anything-V2-Small-F16.gguf"},
        {AIArchitecture::MiGan, "migan", "MI-GAN", Tr::tr("Comblement (remplissage d'après le contenu) par IA"),
         "https://huggingface.co/Acly/MIGAN-GGUF/tree/main", "MIGAN-512-places2-F16.gguf"},
        {AIArchitecture::Esrgan, "esrgan", "Real-ESRGAN", Tr::tr("Agrandissement / super-résolution"),
         "https://huggingface.co/Acly/Real-ESRGAN-GGUF/tree/main", "ESRGAN-4x-foolhardy_Remacri-F16.gguf"},
    };
    return v;
}

const AIArchitectureInfo& info(AIArchitecture a) {
    for (auto& i : all()) if (i.id == a) return i;
    return all().front();
}

static QString settingsKey(AIArchitecture a) { return "ai/model_" + info(a).key; }

QString modelPath(AIArchitecture a) { return QSettings("PhotoClone", "PhotoClone").value(settingsKey(a)).toString(); }

void setModelPath(AIArchitecture a, const QString& path) {
    QSettings s("PhotoClone", "PhotoClone");
    if (path.isEmpty()) s.remove(settingsKey(a)); else s.setValue(settingsKey(a), path);
}

bool isConfigured(AIArchitecture a) { return !modelPath(a).isEmpty(); }

bool libraryAvailable() {
#ifdef PC_HAVE_VISIONCPP
    return true;
#else
    return false;
#endif
}

}
