#include "Icons.h"
#include <QMap>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

QIcon svgIcon(const QString& body, int size) {
    QString svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#e2e2e2' stroke-width='1.6' "
                  "stroke-linecap='round' stroke-linejoin='round'>" + body + "</svg>";
    QSvgRenderer r(svg.toUtf8());
    QIcon icon;
    for (int s : {size, size * 2}) {
        QPixmap px(s, s);
        px.fill(Qt::transparent);
        QPainter p(&px);
        r.render(&p);
        icon.addPixmap(px);
    }
    return icon;
}

static const QMap<QString, QString>& toolSvgs() {
    static const QMap<QString, QString> m = {
        {"move", "<path d='M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3'/>"},
        {"marquee_rect", "<rect x='4' y='6' width='16' height='12' stroke-dasharray='3 2'/>"},
        {"marquee_ellipse", "<ellipse cx='12' cy='12' rx='8' ry='6' stroke-dasharray='3 2'/>"},
        {"lasso", "<path d='M12 5c-5 0-8 2-8 5s3 5 8 5 8-2 8-5-3-5-8-5z'/><path d='M8 15c0 3 1 4 3 5'/>"},
        {"lasso_poly", "<path d='M4 8l6-4 9 3-2 8-7 5-6-6z' stroke-dasharray='3 2'/>"},
        {"wand", "<path d='M4 20L15 9'/><path d='M17 3v4M15 5h4M20 9v3M18.5 10.5h3M8 4v3M6.5 5.5h3'/>"},
        {"crop", "<path d='M7 2v15h15M2 7h15v15'/>"},
        {"eyedropper", "<path d='M15 4l5 5-3 3-5-5z'/><path d='M12 7l-8 8v5h5l8-8'/>"},
        {"brush", "<path d='M20 4c-6 2-9 6-10 9l3 3c3-1 7-4 7-12z'/><path d='M9 14c-3 0-4 2-4 4s-1 2-2 2c3 2 8 1 8-3'/>"},
        {"pencil", "<path d='M4 20l1-5L16 4l4 4L9 19z'/><path d='M14 6l4 4'/>"},
        {"eraser", "<path d='M4 16l9-10 7 6-6 8H8z'/><path d='M9 20h11'/>"},
        {"clone", "<path d='M9 3h6v6l3 3v3H6v-3l3-3z'/><path d='M5 20h14'/>"},
        {"bucket", "<path d='M5 11l7-7 7 7-7 7z'/><path d='M19 14c1.5 2 2 3 2 4a2 2 0 1 1-4 0c0-1 .5-2 2-4z'/>"},
        {"gradient", "<rect x='3' y='6' width='18' height='12'/><path d='M7 6v12M11 6v12M15 6v12' stroke-dasharray='1 2'/>"},
        {"blur", "<path d='M12 3c4 5 6 8 6 11a6 6 0 0 1-12 0c0-3 2-6 6-11z'/>"},
        {"sharpen", "<path d='M12 4l8 15H4z'/>"},
        {"smudge", "<path d='M8 20c-2-3-2-6 0-8V6a1.5 1.5 0 0 1 3 0v5V4a1.5 1.5 0 0 1 3 0v7V6a1.5 1.5 0 0 1 3 0v8c0 3-2 6-4 6z'/>"},
        {"dodge", "<circle cx='12' cy='9' r='5'/><path d='M12 14v7'/>"},
        {"burn", "<path d='M6 20v-8l3-2v-4l3 3 3-3v4l3 2v8z'/>"},
        {"text", "<path d='M5 6V4h14v2M12 4v16M9 20h6'/>"},
        {"shape", "<rect x='4' y='6' width='16' height='12' rx='2'/>"},
        {"hand", "<path d='M8 21c-3-3-4-6-4-8l2-1 2 2V5a1.5 1.5 0 0 1 3 0v6V4a1.5 1.5 0 0 1 3 0v7V6a1.5 1.5 0 0 1 3 0v8c0 4-3 7-6 7z'/>"},
        {"zoom", "<circle cx='10' cy='10' r='6'/><path d='M15 15l6 6M10 7v6M7 10h6'/>"},
        {"sel_transform", "<rect x='4' y='8' width='11' height='9' stroke-dasharray='2 2'/><path d='M13 4a8 8 0 0 1 7 7M20 5v6h-6'/>"},
        {"transform", "<rect x='6' y='6' width='12' height='12'/><path d='M4 4h4M16 4h4M4 20h4M16 20h4'/>"},
    };
    return m;
}

QIcon toolIcon(const QString& id) { return svgIcon(toolSvgs().value(id, "<circle cx='12' cy='12' r='6'/>")); }

QIcon actionIcon(const QString& n) {
    static const QMap<QString, QString> m = {
        {"newlayer", "<rect x='4' y='4' width='16' height='16'/><path d='M12 8v8M8 12h8'/>"},
        {"duplicate", "<rect x='3' y='7' width='13' height='13'/><path d='M8 3h13v13'/>"},
        {"delete", "<path d='M5 7h14M9 7V4h6v3M7 7l1 13h8l1-13'/>"},
        {"mask", "<rect x='3' y='3' width='18' height='18'/><circle cx='12' cy='12' r='5'/>"},
    };
    return svgIcon(m.value(n), 18);
}
