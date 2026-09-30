#include "Effect.h"
#include "Inpaint.h"

// Effets de retouche qui ont besoin de connaître la sélection elle-même (pas seulement le résultat qui lui sera
// appliqué après coup) : contrairement aux filtres et réglages, ils sont enregistrés via addMasked().
void registerRetouch(EffectRegistry& R) {
    R.addMasked(
        "retouch.inpaint", "Remplissage d'après le contenu (comblement)…", "Retouche",
        {
            P::Choice("algo", "Algorithme", {"Navier-Stokes (fluide)", "Telea (rapide, recommandé)", "MI-GAN (IA, modèle requis)"}, 1),
            P::Int("radius", "Rayon de reconstruction (px)", 1, 100, 3),
            P::Int("expand", "Étendre la zone sélectionnée (px)", 0, 100, 0),
            P::Bool("sampleAll", "Échantillonner tous les calques", false),
        },
        [](const cv::Mat& src, const cv::Mat& sel, const Params& p) {
            if (p.i("algo") == 2) {   // MI-GAN : réseau de neurones (index 2, au-delà des constantes cv::INPAINT_*)
                QString err;
                cv::Mat out = inpaintMiGan(src, sel, p.i("expand"), &err);
                if (!err.isEmpty()) EffectDiag::setError(err);
                return out;
            }
            InpaintParams ip;
            ip.algorithm = p.i("algo");
            ip.radius = p.i("radius");
            ip.expand = p.i("expand");
            return inpaintBGRA(src, sel, ip);
        },
        /* requiresSelection */ true, /* supportsSampleAllLayers */ true);
}
