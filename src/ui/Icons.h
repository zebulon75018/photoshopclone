#pragma once
#include <QIcon>
#include <QString>
QIcon svgIcon(const QString& body, int size = 28);      // corps SVG sur grille 24x24, tracé clair
QIcon toolIcon(const QString& toolId);
QIcon actionIcon(const QString& name);                  // newlayer, duplicate, delete, mask
