#include <QApplication>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QPalette>
#include <QStyleFactory>
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PhotoClone");
    app.setOrganizationName("PhotoClone");
    QTranslator qtFr;   // traductions Qt (sélecteur de couleur, boîtes de fichiers…) si le paquet qttranslations est installé
    if (qtFr.load("qt_fr", QLibraryInfo::location(QLibraryInfo::TranslationsPath))) app.installTranslator(&qtFr);
    applyDarkTheme(app);
    MainWindow w;
    w.show();
    for (int i = 1; i < argc; ++i) w.openPath(QString::fromLocal8Bit(argv[i]));
    return app.exec();
}
