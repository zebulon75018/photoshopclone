# PhotoClone — un éditeur d'images façon Photoshop (Qt5 + OpenCV, Linux)

Éditeur raster en C++17 : calques, masques, sélections, filtres, réglages colorimétriques, texte, historique,
espace de travail à onglets/panneaux, raccourcis clavier identiques à Photoshop.


![screen shot](/shot1.png)



## Compilation

```bash
sudo apt install build-essential cmake ninja-build qtbase5-dev libqt5svg5-dev libopencv-dev
cmake -S . -B build -G Ninja
cmake --build build
./build/PhotoClone [image ...]          # ou glisser-déposer des fichiers dans la fenêtre
QT_QPA_PLATFORM=offscreen ./build/smoke_test   # test automatisé (moteur + outils + historique + interface)
```
Testé avec Qt 5.15 / OpenCV 4.6 / GCC 13 (Ubuntu 24.04). OpenMP est utilisé s'il est disponible.

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
│  └─ EffectRegistry  l'UI (dialogue + aperçu en direct) est générée automatiquement depuis les ParamDef
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
| Édition | Copier / couper / coller (presse-papiers système, collage depuis d'autres applis), copier avec fusion, coller sur place, remplir PP/AP, effacer, dernier filtre (Ctrl+F) |
| Espace de travail | **Dialogues déplaçables dans la fenêtre** (position mémorisée), onglets multi-documents, docks, thème sombre, zoom (Alt+molette, Ctrl+±, Ctrl+0/1, outil Zoom), pan (Espace, clic milieu, molette), grille, glisser-déposer de fichiers, mémorisation de la disposition |

## Raccourcis (identiques à Photoshop)
`V` Déplacement · `M` Sélection (Maj+M : ellipse, puis transformation de la sélection) · `L` Lasso (Maj+L polygonal) · `W` Baguette · `C` Recadrage · `I` Pipette ·
`B` Pinceau (Maj+B crayon) · `S` Tampon · `E` Gomme · `G` Dégradé (Maj+G pot) · `R` Goutte/Netteté/Doigt · `O` Densité · `T` Texte ·
`U` Formes · `H` Main · `Z` Zoom · `X` permuter couleurs · `D` couleurs par défaut · `[` `]` taille · `{` `}` dureté ·
`Espace` main temporaire · `Alt` pipette (pinceau) / source (tampon) · `Maj+clic` ligne droite ·
`Ctrl+N/O/S/Maj+S/W` · `Ctrl+Z` / `Ctrl+Maj+Z` · `Ctrl+X/C/V` · `Ctrl+A/D/Maj+D/Maj+I` · `Ctrl+J` · `Ctrl+Maj+N` · `Ctrl+E` ·
`Ctrl+T` · `Ctrl+L/M/U/B/I` · `Ctrl+Maj+U/L` · `Ctrl+F` · `Ctrl+0/1/±` · `Alt+Retour arrière` / `Ctrl+Retour arrière` · `Tab` · `F1` (liste complète).

![Screen Shot ](/shot2.png)


## Transformation de la sélection (outil du groupe `M`, ou menu Sélection)
Des poignées entourent la sélection : glisser une poignée = échelle (Maj : proportionnel, Alt : depuis le centre), glisser
hors du cadre = rotation autour du centre (Maj : pas de 15°), glisser à l'intérieur = déplacer. Les champs L / H / Rotation de la barre
d'options permettent la saisie numérique. Entrée ou double-clic valide, Échap annule ; changer d'outil ou lancer une action de menu valide.

**Contenu** : par défaut, les pixels du **calque actif** situés dans la sélection suivent la transformation, avec aperçu en direct
(l'ancien emplacement est vidé, comme un « couper puis coller transformé »). Pixels et contour sont validés — et annulés — en **une seule
étape d'historique**. Décocher « Transformer le contenu du calque » dans la barre d'options pour ne transformer que le contour.
Si le calque actif est masqué ou verrouillé, seul le contour est transformé (avec un message). Les calques texte sont pixellisés
à la validation. `Ctrl+T` (Transformation manuelle) fait la même chose sur la sélection, ou sur tout le calque s'il n'y en a pas.

## Dialogues déplaçables
Les boîtes de dialogue de l'application (filtres et réglages, tailles, nouveau document, texte, sélecteur de couleur, saisies) sont des
cadres flottants *enfants de la fenêtre principale* : on les déplace en glissant leur barre de titre (elles restent dans la fenêtre,
même sur Wayland), leur position est mémorisée par type de dialogue, et par défaut elles s'ouvrent en haut à droite pour ne pas
masquer l'image. Pendant qu'un dialogue est ouvert, menus, panneaux et raccourcis sont inhibés mais on peut naviguer dans l'image
(main, Espace, molette, Alt+molette) pour juger l'aperçu en direct. Seules les boîtes d'ouverture/enregistrement de fichiers et
les messages d'alerte restent des fenêtres système. Pour en créer une : dériver de `MovableDialog` (l'appel `exec()` reste identique).

## Limites connues / pistes d'évolution
Non implémentés : sélection rapide, lasso magnétique, plume/tracés/calques de forme vectoriels (les formes sont pixellisées),
styles de calque (fx), objets dynamiques, groupes de calques, calques de réglage non destructifs (les réglages sont appliqués
aux pixels), pinceau de correction / correcteur, Fluidité, remplissage d'après contenu, masque de sélection rapide, règles et
repères, navigateur, couches, 16 bits / CMJN / profils ICC, import/export PSD, sensibilité à la pression d'une tablette.
Les calques ont la taille du document (simple et robuste ; un modèle "calque + décalage" serait plus économe en mémoire).
Le texte s'édite via une boîte de dialogue (pas de saisie directement sur le canevas).
