#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include "core/I18n.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PhotoClone");
    app.setOrganizationName("PhotoClone");
    I18n::install();   // langue enregistrée, sinon langue du système (repli : anglais puis français, langue source)
    applyDarkTheme(app);
    MainWindow w;
    w.show();
    for (int i = 1; i < argc; ++i) w.openPath(QString::fromLocal8Bit(argv[i]));
    return app.exec();
}
