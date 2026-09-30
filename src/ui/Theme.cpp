#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

// Thème sombre façon Photoshop.
void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor base(0x32, 0x32, 0x32), alt(0x3c, 0x3c, 0x3c), text(0xe0, 0xe0, 0xe0), hl(0x2d, 0x8c, 0xeb);
    p.setColor(QPalette::Window, base);         p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, QColor(0x26, 0x26, 0x26)); p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, QColor(0x20, 0x20, 0x20)); p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);           p.setColor(QPalette::Button, alt);
    p.setColor(QPalette::ButtonText, text);     p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6c, 0xb6, 0xff));            // liens lisibles sur fond sombre
    p.setColor(QPalette::LinkVisited, QColor(0xb0, 0x9c, 0xff));
    p.setColor(QPalette::Highlight, hl);        p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    app.setPalette(p);
    app.setStyleSheet("QToolTip{color:#eee;background:#202020;border:1px solid #555;}"
                      "QToolBar{border:0;spacing:3px;}"
                      "QToolButton:checked{background:#1f6fbf;border-radius:3px;}"
                      "QDockWidget::title{background:#2a2a2a;padding:4px;}"
                      "QTabBar::tab{padding:5px 12px;background:#2b2b2b;} QTabBar::tab:selected{background:#3c3c3c;}");
}

