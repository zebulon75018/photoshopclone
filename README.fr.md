# PhotoClone — un éditeur d'images façon Photoshop (Qt5 + OpenCV, Linux)

Éditeur raster en C++17 : calques, masques, sélections, filtres, réglages colorimétriques, texte, historique,
espace de travail à onglets/panneaux, raccourcis clavier identiques à Photoshop.

## Compilation

```bash
sudo apt install build-essential cmake ninja-build qtbase5-dev libqt5svg5-dev libopencv-dev
cmake -S . -B build -G Ninja
cmake --build build
./build/PhotoClone [image ...]          # ou glisser-déposer des fichiers dans la fenêtre
QT_QPA_PLATFORM=offscreen ./build/smoke_test   # test automatisé (moteur + outils + historique + interface)
```
Testé avec Qt 5.15 / OpenCV 4.6 / GCC 13 (Ubuntu 24.04) ; compilateur C++20 et CMake ≥ 3.28 requis (imposé par
vision.cpp, une dépendance IA, voir plus bas). OpenMP est utilisé s'il est disponible.

Le menu IA (vision.cpp et Stable Diffusion, voir plus bas) nécessite deux dépendances supplémentaires **compilées
depuis leurs sources** — rien de vendu, aucun fichier `.a`/`.so` dans ce dépôt :
```bash
./depend/fetch-sources.sh   # une fois, après le clone : récupère les sources upstream figées (~1 Go, git+réseau requis)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build          # compile PhotoClone ET les deux dépendances IA ensemble (la première fois est longue)
```
Voir `depend/README.md` pour le détail, pourquoi rien n'est vendu, et les notes de portabilité/GPU/mémoire. Sans
cette étape, PhotoClone compile et s'exécute quand même — les commandes IA restent dans le menu mais se signalent
indisponibles.

## Architecture

```
src/
├─ core/        MODÈLE (aucune dépendance à l'interface, sauf QImage/QPainter pour le texte)
│  ├─ Document        pile de calques, sélection, image composite en cache (recomposition par zones), QUndoStack
│  ├─ Layer           BGRA 8 bits + masque 8 bits + propriétés (opacité, fusion, visibilité, verrou) + texte éditable
│  ├─ BlendModes      18 modes de fusion (séparables et non séparables, formules W3C)
│  ├─ Selection       masques de sélection : formes, baguette magique, combinaison, contour progressif, contours
│  ├─ Commands        PixelCommand (différentiel), LayerStateCommand, StructureCommand, SelectionCommand
│  ├─ Operations      toutes les opérations "métier" (calques, image, sélection, presse-papiers…) = 1 entrée d'historique
│  ├─ ImageIO         PNG/JPEG/TIFF/WebP/BMP/PPM + projet natif .pcl (calques, masques, textes conservés)
│  └─ Workspace       état partagé : couleurs PP/AP, réglages des outils, vue courante
├─ effects/     Effect = fonction pure cv::Mat→cv::Mat + description déclarative des paramètres
│  ├─ Filters         27 filtres          ├─ Adjustments   16 réglages (niveaux, courbes, teinte/sat…)
│  ├─ Retouch         remplissage d'après le contenu (Inpaint.h : cv::inpaint, ou MI-GAN), seul effet "conscient" de la sélection
│  └─ EffectRegistry  l'UI (dialogue + aperçu en direct) est générée automatiquement depuis les ParamDef
├─ ai/          fonctions IA via vision.cpp (voir « Fonctions IA ») : AIBackend (modèles + tâches), AIModels (chemins),
│                 VispBridge (cv::Mat ↔ vision.cpp), MaskRefine (post-traitements OpenCV purs, testables sans modèle)
├─ tools/       patron Stratégie : chaque outil reçoit des ToolEvent (coordonnées image) et dessine son overlay
│  ├─ Tool / ToolManager (groupes d'outils, Maj+touche pour alterner)
│  ├─ TransformBox    cadre interactif (déplacer / échelle par 8 poignées / rotation), partagé par :
│  │    TransformTool (pixels, Ctrl+T) et SelectionTransformTool (contour de la sélection seul)
│  ├─ BrushEngine     moteur de tampons partagé (pinceau, crayon, gomme, clone, flou, netteté, doigt, densité)
│  ├─ FloatingContent contenu "soulevé" partagé par Déplacement et Transformation manuelle
│  └─ Selection/Paint/Move/Other Tools
└─ ui/          MainWindow, CanvasView (zoom/pan/damier/fourmis), OptionsBar, ToolBox, panneaux, dialogues, thème
   └─ MovableDialog   dialogues incorporés et déplaçables dans la fenêtre principale (voir ci-dessous)
```

**Historique** : les commandes conservent des en-têtes `cv::Mat` partagés (pas de copie) ; seuls les traits de pinceau
stockent un différentiel rectangulaire. L'invariant (pile linéaire ⇒ un tampon n'est modifié sur place que s'il est l'état
courant) est vérifié par `smoke_test` : annuler tout puis refaire tout redonne des images identiques au pixel près.

**Ajouter un filtre** : une entrée `R.add("filter.x", "Nom…", "Catégorie", {P::Int(...)}, lambda)` dans `Filters.cpp` — menu, dialogue,
aperçu et undo sont automatiques. **Ajouter un outil** : dériver de `Tool`, l'ajouter dans `ToolManager`, icône dans `Icons.cpp`.

## Fonctionnalités

| Domaine | Contenu |
|---|---|
| Outils | Déplacement, Sélection rect./ellipse, Lasso, Lasso polygonal, Baguette magique, Recadrage, Pipette, Pinceau, Crayon, Gomme, Tampon de duplication, Dégradé (5 types), Pot de peinture, Goutte d'eau, Netteté, Doigt, Densité −/+, Texte, Formes, Main, Zoom, Transformation manuelle |
| Sélection | Nouvelle / ajouter (Maj) / soustraire (Alt) / intersection, contour progressif, agrandir, contracter, lisser, inverser, resélectionner, fourmis marchantes, **Transformation de la sélection** (agrandir / réduire / pivoter / déplacer le contour sans toucher aux pixels, saisie numérique L/H/angle) |
| Calques | Nouveau, dupliquer, supprimer, réordonner (glisser-déposer), fusion (bas / visibles / aplatir), 18 modes, opacité, verrou, visibilité, masques de fusion (peinture sur masque), calques texte éditables, calque par copier/couper |
| Image | Taille, zone de travail (ancrage), recadrage, rotations, miroirs |
| Colorimétrie | Luminosité/contraste, niveaux, courbes (RVB+canaux), exposition, teinte/saturation, vibrance, balance des couleurs, N&B, filtre photo, ombres/hautes lumières, négatif, désaturation, seuil, isohélie, auto (tons, contraste, couleur) ; panneau Histogramme, Couleur, Nuancier |
| Filtres | Flous (gaussien, moyen, mouvement, surface, médiane), netteté (accentuation, masque flou, détails), bruit, relief, contours, dessin animé, croquis, aquarelle, mosaïque, torsion, ondulation, sphérisation, vignettage, passe-haut, min/max, nuages |
| Édition | Copier / couper / coller (presse-papiers système, collage depuis d'autres applis), copier avec fusion, coller sur place, remplir PP/AP, effacer, **remplissage d'après le contenu (Maj+F5, inpainting OpenCV)**, dernier filtre (Ctrl+F) |
| IA (vision.cpp) | **Suppression de l'arrière-plan** (BiRefNet, paramétrable, 4 modes de sortie), **outil de sélection par IA** (MobileSAM : clic ou cadre), **carte de profondeur** (Depth-Anything V2), **comblement MI-GAN**, **agrandissement IA** (Real-ESRGAN) — dans le nouveau menu **IA** ; nécessite de télécharger les fichiers de modèle (non fournis) |
| Espace de travail | **Dialogues déplaçables dans la fenêtre** (position mémorisée), onglets multi-documents, docks, thème sombre, zoom (Alt+molette, Ctrl+±, Ctrl+0/1, outil Zoom), pan (Espace, clic milieu, molette), grille, glisser-déposer de fichiers, mémorisation de la disposition |

## Raccourcis (identiques à Photoshop)
`V` Déplacement · `M` Sélection (Maj+M : ellipse, puis transformation de la sélection) · `L` Lasso (Maj+L polygonal) · `W` Baguette · `C` Recadrage · `I` Pipette ·
`B` Pinceau (Maj+B crayon) · `S` Tampon · `E` Gomme · `G` Dégradé (Maj+G pot) · `R` Goutte/Netteté/Doigt · `O` Densité · `T` Texte ·
`U` Formes · `H` Main · `Z` Zoom · `X` permuter couleurs · `D` couleurs par défaut · `[` `]` taille · `{` `}` dureté ·
`Espace` main temporaire · `Alt` pipette (pinceau) / source (tampon) · `Maj+clic` ligne droite ·
`Ctrl+N/O/S/Maj+S/W` · `Ctrl+Z` / `Ctrl+Maj+Z` · `Ctrl+X/C/V` · `Ctrl+A/D/Maj+D/Maj+I` · `Ctrl+J` · `Ctrl+Maj+N` · `Ctrl+E` ·
`Ctrl+T` · `Ctrl+L/M/U/B/I` · `Ctrl+Maj+U/L` · `Ctrl+F` · `Maj+F5` remplissage d'après le contenu · `Ctrl+Alt+K` supprimer l'arrière-plan (IA) · `Maj+W` sélection par IA · `Ctrl+0/1/±` · `Alt+Retour arrière` / `Ctrl+Retour arrière` · `Tab` · `F1` (liste complète).

## Transformation de la sélection (outil du groupe `M`, ou menu Sélection)
Des poignées entourent la sélection : glisser une poignée = échelle (Maj : proportionnel, Alt : depuis le centre), glisser
hors du cadre = rotation autour du centre (Maj : pas de 15°), glisser à l'intérieur = déplacer. Les champs L / H / Rotation de la barre
d'options permettent la saisie numérique. Entrée ou double-clic valide, Échap annule ; changer d'outil ou lancer une action de menu valide.

**Contenu** : par défaut, les pixels du **calque actif** situés dans la sélection suivent la transformation, avec aperçu en direct
(l'ancien emplacement est vidé, comme un « couper puis coller transformé »). Pixels et contour sont validés — et annulés — en **une seule
étape d'historique**. Décocher « Transformer le contenu du calque » dans la barre d'options pour ne transformer que le contour.
Si le calque actif est masqué ou verrouillé, seul le contour est transformé (avec un message). Les calques texte sont pixellisés
à la validation. `Ctrl+T` (Transformation manuelle) fait la même chose sur la sélection, ou sur tout le calque s'il n'y en a pas.

## Remplissage d'après le contenu (menu Édition, ou `Maj+F5`)
Comble la zone sélectionnée en la reconstruisant à partir de son voisinage, via `cv::inpaint` d'OpenCV — ce n'est pas
l'algorithme propriétaire de Photoshop (qui utilise du patch-matching avancé et parfois un modèle génératif), mais les
deux algorithmes classiques d'OpenCV, efficaces pour effacer de petits défauts (poussière, rayure, petit objet, câble…)
sur un fond relativement uniforme ou texturé. Une sélection est requise ; l'action est refusée (avec un message) sinon.

Paramètres, tous réglables dans la boîte de dialogue avec aperçu en direct :
- **Algorithme** : *Telea* (rapide, basé sur la marche du front — recommandé dans la plupart des cas) ou *Navier-Stokes*
  (basé sur la propagation de niveaux de gris façon fluide, parfois meilleur sur de grandes zones lisses).
- **Rayon de reconstruction (px)** : taille du voisinage utilisé pour reconstruire chaque pixel (défaut 3 px). Une valeur
  plus grande lisse davantage mais coûte plus cher en temps de calcul.
- **Étendre la zone sélectionnée (px)** : agrandit (dilate) la zone à reconstruire avant de lancer l'algorithme, pour
  supprimer un liseré résiduel de l'objet effacé si la sélection le serrait de trop près (défaut 0).
- **Échantillonner tous les calques** : par défaut, la reconstruction n'utilise que les pixels du **calque actif** —
  si ce calque est transparent autour de la zone sélectionnée (par exemple un calque de retouche isolé), le résultat
  sera basé sur cette transparence. Cocher cette case reconstruit à partir de l'image fusionnée (tous les calques
  visibles) mais peint tout de même le résultat uniquement sur le calque actif, comme un « copier avec fusion » ciblé.

Le résultat est peint uniquement à l'intérieur de la sélection (bords adoucis si la sélection a un contour progressif),
en une seule étape d'historique, annulable normalement. Si le calque actif était transparent dans la zone reconstruite,
il devient opaque à cet endroit (on vient d'y peindre une nouvelle couleur).

## Fonctions IA (vision.cpp)
PhotoClone intègre [vision.cpp](https://github.com/Acly/vision.cpp) (inférence basée sur ggml, CPU) pour cinq modèles neuronaux. Tout
s'exécute en local, rien n'est envoyé sur le réseau. Les commandes sont dans le nouveau menu **IA** ; les modèles se configurent une fois
dans **IA ▸ Réglages des modèles…** (un fichier `.gguf` par modèle, avec liens de téléchargement et validation immédiate de chaque fichier).

> **Les poids des modèles ne sont pas fournis** (centaines de Mo, licences propres — voir `depend/README.md` pour la compilation de la bibliothèque vision.cpp elle-même). Sans eux,
> les commandes IA proposent d'ouvrir les réglages au lieu d'échouer. Dans l'environnement où cette intégration a été développée, le site
> d'hébergement des modèles était inaccessible : **l'inférence neuronale elle-même n'a donc jamais été exécutée de bout en bout**. Ce qui est
> testé (`tests/smoke_test.cpp`) : la bibliothèque vision.cpp compile, se lie et s'exécute, la conversion d'images, les post-traitements de
> masque/profondeur, toutes les opérations sur le document, la validation et le refus propre de fichiers de modèle absents/mauvais/corrompus,
> les dialogues (pilotés avec un fournisseur de masque de substitution) et les chemins d'erreur sans modèle. Signalez tout problème rencontré avec de vrais modèles.

| Commande | Modèle | Effet |
|---|---|---|
| **IA ▸ Supprimer l'arrière-plan…** (`Ctrl+Alt+K`) | BiRefNet | Détourage automatique du sujet, voir ci-dessous |
| Outil **Sélection par IA** (groupe `W`, `Maj+W`) | MobileSAM | *Cliquer* sur un objet, ou *tracer un cadre* autour → sélection. `Maj` ajoute, `Alt` soustrait, options contour progressif et « tous les calques ». L'image est analysée une fois (quelques secondes sur CPU) puis chaque clic est quasi instantané ; ré-analyse automatique si le document a changé |
| **IA ▸ Remplissage IA (MI-GAN)…** | MI-GAN | Même dialogue que le remplissage d'après le contenu (`Maj+F5`) avec l'algorithme MI-GAN (aussi sélectionnable dedans). Le modèle travaille à résolution fixe 256/512 px : PhotoClone lui donne un carré rogné autour de la sélection (~2× sa taille, pour le contexte) et recolle le résultat |
| **IA ▸ Carte de profondeur…** | Depth-Anything V2 | Nouveau calque « Profondeur » en niveaux de gris et/ou sélection par seuil de profondeur (zones proches ou lointaines). Options : inverser (la polarité dépend du fichier), contraste auto, lissage |
| **IA ▸ Agrandissement IA…** | Real-ESRGAN | Agrandit **tout le document** (chaque calque et masque ; une passe du modèle par calque ; calques texte pixellisés) par le facteur natif du modèle (souvent ×4) ou un facteur final personnalisé. Les zones transparentes reçoivent d'abord une propagation de couleur (pas de liseré sombre) ; l'alpha est redimensionné classiquement. Refuse au-delà de 64 Mpx (sécurité mémoire) |

### Suppression de l'arrière-plan
`IA ▸ Supprimer l'arrière-plan…` lance BiRefNet **une seule fois** à l'ouverture du dialogue (quelques secondes sur CPU : voir les mesures amont dans
le modèle *lite* est bien plus rapide que le complet — voir le README de vision.cpp pour des repères de performance) puis permet d'ajuster le résultat avec un **aperçu
instantané** (seuls de légers post-traitements OpenCV sont rejoués, pas le réseau) :
- **Seuil** — 0 conserve le détourage doux (cheveux, poils, verre) ; >0 donne un bord net ;
- **Contracter/étendre** (px) et **contour progressif** (px) — corrigent halos et bords crénelés ;
- **Ne garder que le plus grand sujet** — supprime les îlots parasites ; **Inverser** — garde l'arrière-plan à la place ;
- **Sortie** — *masque de fusion* sur le calque actif (non destructif, **par défaut**), *nouveau calque avec le sujet seul* (fond transparent),
  *remplacer le calque actif* (transparence appliquée à ses pixels) ou *sélection uniquement* ;
- **Corriger les couleurs de bord** (rayon) — supprime le liseré de couleur de l'ancien fond (estimation du premier plan de vision.cpp ; modes nouveau calque / remplacer) ;
- **Analyser l'image fusionnée** — analyse tous les calques visibles au lieu du calque actif ; **Recalculer** relance le réseau.
Toute la commande forme une seule étape d'historique. L'appel au réseau est synchrone : l'interface est figée pendant le calcul (pas encore de fil dédié).

**Portabilité** : la bibliothèque statique est compilée pour x86_64 Linux, CPU seulement, base AVX2 (Haswell 2013+, volontairement pas
`-march=native`). Pour une autre plateforme, le GPU (Vulkan) ou une autre base CPU, voir `depend/README.md`.
Le projet requiert C++20 et CMake ≥ 3.28. Sans `depend/visioncpp-src` (lancez `./depend/fetch-sources.sh` pour l'obtenir),
PhotoClone compile quand même ; les commandes IA signalent alors l'absence de la bibliothèque.

## Stable Diffusion (stable-diffusion.cpp)
PhotoClone intègre également [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) pour la génération d'image via un
prompt et l'inpainting sur une sélection. Contrairement aux modèles vision.cpp ci-dessus, **le calcul s'exécute dans son propre fil et
peut être annulé en cours de génération** : le dialogue reste réactif (barre de progression, temps restant estimé étape par étape,
journal en direct), et le reste de l'application (menus, panneaux) est verrouillé comme pour les autres dialogues, mais le canevas reste
navigable (déplacement, zoom).

| Commande | Ce qu'elle fait |
|---|---|
| **IA ▸ Générer une image (Stable Diffusion)…** (`Ctrl+Alt+G`) | Texte vers image. Choisissez une taille (multiple de 8), un prompt/prompt négatif, le nombre d'étapes, le CFG, l'échantillonneur/ordonnanceur, une graine, et jusqu'à 8 variantes en un seul lot. Le résultat est ajouté comme nouveau calque (placement : centré/ajusté/rempli/dans la sélection actuelle) ou, si aucun document n'est ouvert, crée un nouveau document |
| **IA ▸ Inpainting sur la sélection (Stable Diffusion)…** (`Ctrl+Alt+P`) | Régénère la sélection actuelle selon un prompt. PhotoClone recadre un carré avec marge de contexte autour de la sélection, le met à l'échelle de travail du modèle (512 pour SD 1.x, 1024 pour SDXL, réglable), l'envoie avec un masque, puis recolle le résultat dans le document avec un raccord adouci — tout ce qui est hors de la sélection (dilatée, adoucie) reste identique au bit près |
| **IA ▸ Réglages de Stable Diffusion…** | Trois onglets : « Checkpoint complet » (cas courant : un seul fichier), « Modèle de diffusion + encodeurs » (cas avancé : Flux, SD3…), « Performances » (fils de calcul, attention flash, projection mémoire, libération après usage). Un statut global en bas indique quelle méthode sera utilisée. Chaque champ a un bouton diagnostic (icône ⓘ) qui inspecte le fichier — voir ci-dessous |
| **IA ▸ Libérer la mémoire des modèles IA** | Libère à la fois le cache de Stable Diffusion et celui des modèles vision.cpp |

**Diagnostic d'un fichier de modèle** (bouton ⓘ à côté de chaque champ) : avant même d'essayer de charger le fichier dans
stable-diffusion.cpp, PhotoClone l'inspecte directement — sans dépendre de la bibliothèque — pour dire ce qu'il a trouvé : format
détecté (GGUF, safetensors, archive zip PyTorch `.ckpt`/`.pt`), cohérence structurelle (taille attendue vs réelle, archive tronquée,
en-tête corrompu…), nombre de tenseurs, aperçu de leurs noms, métadonnées, et une interprétation heuristique de l'architecture (UNet
SD 1.x/2.x/SDXL, blocs Flux/SD3, VAE, encodeur CLIP/T5 — détectés par les noms de tenseurs pour GGUF/safetensors, ou par une recherche
de motifs dans le pickle non désérialisé pour les `.ckpt`) qui dit si le fichier devrait suffire seul ou s'il faut le compléter dans
l'autre onglet. Un bouton séparé permet de calculer le SHA-256 (relit tout le fichier : plusieurs secondes sur un gros checkpoint,
donc pas fait par défaut). Le lecteur d'archive zip gère le format Zip64, nécessaire au-delà de 4 Go (le cas normal pour un
checkpoint complet) ; testé sur une archive réelle de 4,3 Go de ce format.

**Annulation** : l'API C native (`sd_cancel_generation`) est réellement annulable — contrôlée à plusieurs points de la boucle
d'échantillonnage — donc « Annuler le calcul » s'arrête en général en environ une étape d'échantillonnage, pas à la fin de toute la
génération restante. Deux particularités de la bibliothèque ont été contournées : le drapeau d'annulation est remis à zéro en interne au
tout début de chaque appel, donc PhotoClone le ré-arme à chaque rappel de progression plutôt que de se fier à un seul appel à
`cancel()` ; et annuler signifie *abandonner*, pas *garder le résultat partiel* — si le modèle renvoie quand même des images après une
demande d'annulation, elles sont écartées. Le **chargement** du modèle ne peut pas être interrompu (l'annulation prend effet dès qu'il
se termine). Fermer un dialogue de génération pendant qu'il tourne l'annule et diffère la fermeture réelle jusqu'à l'arrêt effectif du fil.

**Isolation vis-à-vis de vision.cpp** : stable-diffusion.cpp embarque sa propre copie de ggml (une version différente de celle liée
statiquement par vision.cpp). Pour éviter tout conflit de symboles, elle est compilée en bibliothèque partagée séparée,
`depend/stablediffusioncpp/lib/libpcsd.so`, avec un script de version du lieur n'exposant que la vingtaine de fonctions de l'API C dont
PhotoClone a besoin et **zéro** symbole ggml (vérifié avec `nm -D`) ; PhotoClone la charge à l'exécution via `dlopen`/`QLibrary`, sans
jamais toucher au ggml lié statiquement de vision.cpp. Sans `depend/stablediffusioncpp-src` (lancez `./depend/fetch-sources.sh` pour
l'obtenir), ce fichier n'est simplement jamais construit, et l'application compile et démarre quand même — les commandes
Stable Diffusion signalent alors que la bibliothèque est indisponible.

> **Aucun poids de modèle n'est fourni** (plusieurs Go, licences séparées). Configurez le vôtre dans **IA ▸ Réglages de Stable
> Diffusion…** ; le fichier est vérifié par la signature de son en-tête (magique GGUF / en-tête JSON safetensors / zip ou pickle pour
> `.ckpt`) avant usage, et les commandes proposent d'ouvrir les réglages si rien n'est configuré. Dans l'environnement où cette
> intégration a été développée, le site d'hébergement des modèles était inaccessible : **la génération d'image réelle n'a donc jamais
> été exécutée de bout en bout** — aucune vraie image n'est jamais sortie de ce code. Ce qui a été testé (voir `tests/smoke_test.cpp`) :
> la bibliothèque compile, se lie en objet partagé isolé et se charge à l'exécution ; des fichiers de modèle invalides mais plausibles
> (en-têtes GGUF/safetensors/zip fabriqués) sont rejetés proprement par la vraie bibliothèque sans plantage ; la géométrie de recadrage/
> mise à l'échelle/masque/recollage de l'inpainting (OpenCV pur, sans modèle) ; et surtout, la mécanique de fil et d'annulation elle-même
> — avec un vrai `QThread` et une fonction de génération de substitution injectée — vérifiée pour : tourner hors du fil de l'interface,
> garder l'interface réactive, livrer la progression dans l'ordre, annuler en environ une étape, ne jamais perdre une annulation
> précoce, écarter le résultat d'un calcul « terminé mais annulé », refuser une seconde génération simultanée, survivre à une exception
> levée en cours de génération, et refuser les requêtes invalides (taille incorrecte, masque vide…) avant même de créer un fil. Un test
> par mutation (casser temporairement `cancel()`) a confirmé que 5 tests échouent comme attendu, puis repassent une fois le code
> restauré. Merci de signaler tout problème rencontré avec de vrais modèles.

**Portabilité** : `libpcsd.so` est x86_64 Linux, CPU seulement, base AVX2 (Haswell 2013+, pas `-march=native`), même logique que
vision.cpp. Pour une autre plateforme, un moteur GPU, ou une autre base CPU, recompilez-la — voir
`depend/README.md`.

## Dialogues déplaçables
Les boîtes de dialogue de l'application (filtres et réglages, tailles, nouveau document, texte, sélecteur de couleur, saisies) sont des
cadres flottants *enfants de la fenêtre principale* : on les déplace en glissant leur barre de titre (elles restent dans la fenêtre,
même sur Wayland), leur position est mémorisée par type de dialogue, et par défaut elles s'ouvrent en haut à droite pour ne pas
masquer l'image. Pendant qu'un dialogue est ouvert, menus, panneaux et raccourcis sont inhibés mais on peut naviguer dans l'image
(main, Espace, molette, Alt+molette) pour juger l'aperçu en direct. Seules les boîtes d'ouverture/enregistrement de fichiers et
les messages d'alerte restent des fenêtres système. Pour en créer une : dériver de `MovableDialog` (l'appel `exec()` reste identique).

## Limites connues / pistes d'évolution
Non implémentés : lasso magnétique (un outil IA « cliquer pour sélectionner » existe, voir ci-dessus), plume/tracés/calques de forme vectoriels (les formes sont pixellisées),
styles de calque (fx), objets dynamiques, groupes de calques, calques de réglage non destructifs (les réglages sont appliqués
aux pixels), pinceau de correction / correcteur, Fluidité, masque de sélection rapide, règles et repères, navigateur, couches,
16 bits / CMJN / profils ICC, import/export PSD, sensibilité à la pression d'une tablette. Le remplissage d'après le contenu
utilise l'inpainting classique d'OpenCV (Telea / Navier-Stokes), pas le moteur propriétaire de Photoshop (patch-matching
avancé, voire génératif).
Les calques ont la taille du document (simple et robuste ; un modèle "calque + décalage" serait plus économe en mémoire).
Le texte s'édite via une boîte de dialogue (pas de saisie directement sur le canevas).
