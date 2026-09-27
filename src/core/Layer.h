#pragma once
#include <memory>
#include <optional>
#include <opencv2/core.hpp>
#include <QString>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include "BlendModes.h"

// Données d'un calque texte (le calque reste éditable tant qu'il n'est pas pixellisé).
struct TextData {
    QString text;
    QString family = "Sans Serif";
    int pixelSize = 48;
    bool bold = false, italic = false, underline = false;
    QColor color = Qt::black;
    int align = 0;          // 0 gauche, 1 centre, 2 droite
    QPointF pos;            // coin supérieur gauche dans le document
};

struct LayerProps {
    QString name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.f;
    BlendMode blend = BlendMode::Normal;
    bool maskEnabled = true;
};

// Instantané d'un calque : copie d'en-têtes cv::Mat (pas de copie de pixels). Voir Commands.h pour l'invariant.
struct LayerState {
    LayerProps props;
    cv::Mat image, mask;
    std::optional<TextData> text;
};

class Layer {
public:
    using Ptr = std::shared_ptr<Layer>;
    static Ptr create(const QString& name, const cv::Mat& bgra);
    static Ptr createEmpty(const QString& name, QSize size);

    int id() const { return m_id; }
    QSize size() const { return {image.cols, image.rows}; }
    bool isText() const { return text.has_value(); }
    bool hasMask() const { return !mask.empty(); }

    LayerState state() const { return {props, image, mask, text}; }
    void setState(const LayerState& s) { props = s.props; image = s.image; mask = s.mask; text = s.text; }
    Ptr clone() const;                 // copie profonde, nouvel identifiant

    void renderText();                 // (re)génère `image` depuis `text`
    QRectF textBounds() const;

    cv::Mat image;                     // BGRA 8 bits, taille du document
    cv::Mat mask;                      // vide ou CV_8UC1 (taille du document)
    LayerProps props;
    std::optional<TextData> text;
    bool editingMask = false;          // état d'interface : la peinture cible le masque

private:
    Layer() = default;
    int m_id = 0;
};
