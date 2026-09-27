#pragma once
#include <memory>
#include <QString>
class Document;

namespace ImageIO {
// Ouvre une image raster (PNG, JPEG, TIFF, BMP, WEBP, PPM...) ou un projet natif .pcl. Retourne nullptr en cas d'erreur.
Document* open(const QString& path, QString* error);
// Enregistre selon l'extension : .pcl = projet avec calques, autre = image aplatie.
bool save(Document* doc, const QString& path, QString* error, int jpegQuality = 92);
QString openFilter();
QString saveFilter();
bool isNativeProject(const QString& path);
}
