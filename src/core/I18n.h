#pragma once
// Langue de l'interface. Les chaînes du code source sont en français (tr() / Tr::tr()) ; chaque autre langue vient d'un
// fichier translations/photoclone_<code>.ts, compilé en .qm et embarqué dans l'exécutable (ressource :/i18n).
// Ajouter une langue : voir translations/README.md.
#include <QString>
#include <QStringList>

namespace I18n {
// Installe la traduction de l'application et celle de Qt (boîtes de dialogue standard). `code` vide : réglage enregistré,
// sinon langue du système (repli sur l'anglais si elle n'est pas traduite). Rappelable (remplace la précédente), mais
// l'interface déjà construite n'est pas retraduite : à appeler juste après la création de QApplication.
void install(const QString& code = QString());
QString current();                          // code effectif : « fr », « en »…
QStringList available();                    // « fr » (langue source) + langues embarquées
QString displayName(const QString& code);   // nom de la langue dans cette langue : « Français », « English »
QString preferred();                        // réglage enregistré (vide = automatique)
void setPreferred(const QString& code);     // vide = automatique ; prend effet au prochain démarrage
}
