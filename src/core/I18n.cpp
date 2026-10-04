#include "I18n.h"
#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

// Les .qm sont compilés dans photoclone_lib (bibliothèque statique) : référencer la ressource force l'éditeur de liens à
// la garder. Q_INIT_RESOURCE doit être appelé hors de tout espace de noms.
static void initResources() {
#ifdef PC_HAVE_I18N
    Q_INIT_RESOURCE(i18n);
#endif
}

namespace I18n {
namespace {
const QString kSource = QStringLiteral("fr");
QTranslator* g_app = nullptr;
QTranslator* g_qt = nullptr;
QString g_current = kSource;

QString resolve(const QString& wanted) {
    const QStringList avail = available();
    if (avail.contains(wanted)) return wanted;
    for (const QString& ui : QLocale::system().uiLanguages()) {   // ex. « en-US », « fr-FR »
        const QString code = ui.section('-', 0, 0).section('_', 0, 0).toLower();
        if (avail.contains(code)) return code;
    }
    return avail.contains("en") ? "en" : kSource;                  // langue du système non traduite : l'anglais est le repli le plus utile
}

void replace(QTranslator*& slot) {
    if (slot) { QCoreApplication::removeTranslator(slot); delete slot; }
    slot = new QTranslator;
}
}

QStringList available() {
    initResources();
    QStringList codes{kSource};
    for (const QString& f : QDir(":/i18n").entryList({"photoclone_*.qm"}, QDir::Files))
        codes << f.mid(int(strlen("photoclone_"))).section('.', 0, 0);
    codes.removeDuplicates();
    codes.sort();
    return codes;
}

void install(const QString& code) {
    const QString lang = resolve(code.isEmpty() ? preferred() : code);
    replace(g_app);
    replace(g_qt);
    if (lang != kSource && g_app->load("photoclone_" + lang, ":/i18n")) QCoreApplication::installTranslator(g_app);
    // Traductions de Qt lui-même (sélecteur de couleur, boîtes de fichiers…), si le paquet qttranslations est installé.
    // Inutile en anglais : c'est la langue native de Qt.
    const QString qtDir = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    if (lang != "en" && (g_qt->load("qt_" + lang, qtDir) || g_qt->load("qtbase_" + lang, qtDir))) QCoreApplication::installTranslator(g_qt);
    g_current = lang;
}

QString current() { return g_current; }

QString displayName(const QString& code) {
    const QString n = QLocale(code).nativeLanguageName();
    return n.isEmpty() ? code : n.left(1).toUpper() + n.mid(1);
}

QString preferred() { return QSettings("PhotoClone", "PhotoClone").value("language").toString(); }

void setPreferred(const QString& code) {
    QSettings s("PhotoClone", "PhotoClone");
    if (code.isEmpty()) s.remove("language"); else s.setValue("language", code);
}
}
