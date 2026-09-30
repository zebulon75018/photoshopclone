#pragma once
// Métadonnées des 5 architectures IA (vision.cpp : https://github.com/Acly/vision.cpp) et chemin de leur fichier
// de poids .gguf, mémorisé dans les réglages de l'application (indépendant par document : un seul jeu de modèles
// pour toute l'installation). Ce fichier ne dépend pas de vision.cpp lui-même : il reste utile (afficher la boîte
// de dialogue de réglages, etc.) même si la bibliothèque n'est pas compilée (voir PC_HAVE_VISIONCPP).
#include <QString>
#include <vector>

enum class AIArchitecture { Sam, BiRefNet, DepthAnything, MiGan, Esrgan };

struct AIArchitectureInfo {
    AIArchitecture id;
    QString key;          // clé de réglage stable ("sam", "birefnet"...)
    QString name;          // nom affiché
    QString task;          // description courte de la tâche
    QString downloadUrl;   // page HuggingFace où télécharger un .gguf compatible
    QString suggestedFile; // nom de fichier suggéré (informatif)
};

namespace AIModels {
const std::vector<AIArchitectureInfo>& all();
const AIArchitectureInfo& info(AIArchitecture);

QString modelPath(AIArchitecture);              // chemin enregistré, ou chaîne vide si non configuré
void setModelPath(AIArchitecture, const QString& path);
bool isConfigured(AIArchitecture);

// Vrai si la bibliothèque vision.cpp a été compilée dans cette build (voir CMakeLists.txt / PC_HAVE_VISIONCPP).
bool libraryAvailable();
}
