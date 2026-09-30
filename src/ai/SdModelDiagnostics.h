#pragma once
// Diagnostic d'un fichier de modèle (checkpoint / diffusion seul / VAE / encodeur de texte), SANS passer par
// stable-diffusion.cpp : lecture directe du conteneur (GGUF / safetensors / archive zip PyTorch / pickle brut).
// But : dire à la personne CE QUI a été détecté (format, nombre de tenseurs, un aperçu de leurs noms, une estimation de
// l'architecture) et POURQUOI un fichier est refusé (taille incohérente, archive tronquée…), avant même d'essayer de
// charger quoi que ce soit dans la bibliothèque réelle.
#include <QString>
#include <QStringList>

namespace Sd {

struct ModelDiagnostic {
    bool exists = false;
    bool readable = false;
    qint64 sizeBytes = -1;

    QString format;                 // libellé humain : « GGUF », « Safetensors », « Archive zip PyTorch (.ckpt/.pt) », « Pickle brut (ancien .ckpt) », « Inconnu »
    bool structurallyValid = false; // le conteneur a pu être analysé correctement (pas juste « ça commence par la bonne signature »)
    QStringList issues;             // problèmes détectés : incohérences de taille, archive tronquée, en-tête corrompu…

    qint64 tensorCount = -1;        // -1 = inconnu (format non structuré en tenseurs nommés, ou non déterminable)
    QStringList sampleNames;        // aperçu de noms de tenseurs / entrées de l'archive (limité)
    QStringList metadata;           // lignes « clé : valeur » (métadonnées GGUF, __metadata__ safetensors…)
    QString archGuess;              // phrase d'interprétation sur l'architecture probable (peut être vide si indéterminable)

    QString sha256;                 // vide si non demandé
};

// `computeSha256` : lecture complète du fichier en plus de l'analyse structurelle (coûteux sur un fichier de plusieurs Go :
// quelques secondes à quelques dizaines de secondes). Toujours sûr : ne lance jamais d'exception, ne charge jamais
// tout le fichier en mémoire (lecture par blocs), et s'arrête proprement sur un fichier corrompu ou tronqué.
ModelDiagnostic diagnoseModelFile(const QString& path, bool computeSha256 = false);

// Met en forme un diagnostic en texte français lisible, prêt à afficher ou à copier.
QString formatDiagnostic(const ModelDiagnostic&);

}
