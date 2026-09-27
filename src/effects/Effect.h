#pragma once
// Infrastructure des effets (filtres ET réglages colorimétriques) : un effet = une fonction pure
// cv::Mat(BGRA) -> cv::Mat(BGRA) + une description déclarative de ses paramètres (l'UI est générée automatiquement).
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <opencv2/core.hpp>
#include <QColor>
#include <QMap>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QVariant>

struct ParamDef {
    enum Type { Int, Double, Bool, Choice, Color, Curve } type = Int;
    QString key, label;
    double min = 0, max = 100;
    QVariant def;
    QStringList choices;
    int decimals = 0;
};

class Params {
public:
    int i(const QString& k) const { return m.value(k).toInt(); }
    double d(const QString& k) const { return m.value(k).toDouble(); }
    bool b(const QString& k) const { return m.value(k).toBool(); }
    QColor color(const QString& k) const { return m.value(k).value<QColor>(); }
    QPolygonF curve(const QString& k) const { return m.value(k).value<QPolygonF>(); }
    void set(const QString& k, const QVariant& v) { m[k] = v; }
    QVariantMap m;
};

namespace P {   // fabriques de ParamDef
inline ParamDef Int(const QString& k, const QString& l, int mn, int mx, int def) { ParamDef p; p.type = ParamDef::Int; p.key = k; p.label = l; p.min = mn; p.max = mx; p.def = def; return p; }
inline ParamDef Dbl(const QString& k, const QString& l, double mn, double mx, double def, int dec = 2) { ParamDef p; p.type = ParamDef::Double; p.key = k; p.label = l; p.min = mn; p.max = mx; p.def = def; p.decimals = dec; return p; }
inline ParamDef Bool(const QString& k, const QString& l, bool def) { ParamDef p; p.type = ParamDef::Bool; p.key = k; p.label = l; p.def = def; return p; }
inline ParamDef Choice(const QString& k, const QString& l, const QStringList& ch, int def = 0) { ParamDef p; p.type = ParamDef::Choice; p.key = k; p.label = l; p.choices = ch; p.def = def; return p; }
inline ParamDef Color(const QString& k, const QString& l, QColor def) { ParamDef p; p.type = ParamDef::Color; p.key = k; p.label = l; p.def = def; return p; }
inline ParamDef Curve(const QString& k, const QString& l) { ParamDef p; p.type = ParamDef::Curve; p.key = k; p.label = l; QPolygonF poly; poly << QPointF(0, 0) << QPointF(1, 1); p.def = QVariant::fromValue(poly); return p; }
}

class Effect {
public:
    using Fn = std::function<cv::Mat(const cv::Mat& srcBGRA, const Params&)>;
    QString id, name, category;
    std::vector<ParamDef> defs;
    Fn fn;

    Params defaults() const { Params p; for (auto& d : defs) p.set(d.key, d.def); return p; }
    cv::Mat run(const cv::Mat& src, const Params& p) const { return fn(src, p); }
};
using EffectPtr = std::shared_ptr<Effect>;

class EffectRegistry {
public:
    static EffectRegistry& instance();
    const std::vector<EffectPtr>& all() const { return m_all; }
    EffectPtr find(const QString& id) const;
    QStringList categories(const QString& prefix) const;   // ex. "filter" ou "adjust"
    void add(const QString& id, const QString& name, const QString& category, std::vector<ParamDef> defs, Effect::Fn fn);
private:
    EffectRegistry();
    std::vector<EffectPtr> m_all;
};

void registerFilters(EffectRegistry&);
void registerAdjustments(EffectRegistry&);

// Applique `result` sur `orig` en respectant la sélection (masque doux) : out = orig*(1-s) + result*s
cv::Mat blendWithSelection(const cv::Mat& orig, const cv::Mat& result, const cv::Mat& sel);

// LUT 256 entrées d'une courbe (points de contrôle dans [0,1]²), interpolation monotone cubique.
std::array<unsigned char, 256> curveLut(QPolygonF pts);
