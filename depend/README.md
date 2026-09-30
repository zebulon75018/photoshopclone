# Dépendances IA optionnelles : vision.cpp et stable-diffusion.cpp

PhotoClone a deux dépendances IA **optionnelles**, toutes deux compilées à partir de leurs sources upstream — **rien
n'est vendu ni committé dans ce dépôt** (pas de `.a`, pas de `.so`, pas de copie de leur code). Sans elles, PhotoClone
compile et s'exécute normalement ; seules les commandes du menu **IA** sont indisponibles (elles le signalent
clairement plutôt que de planter).

```
./depend/fetch-sources.sh          # une fois, après le clone du dépôt (ou --force pour retélécharger)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                # compile PhotoClone ET les deux dépendances, en une seule commande
```

## Pourquoi pas de binaires dans le dépôt Git

Un dépôt GitHub ne devrait pas contenir de fichiers `.a`/`.so` compilés : ils gonflent l'historique définitivement
(même supprimés plus tard, ils restent dans les anciens commits), sont opaques (impossibles à relire/auditer/« diff
»), et sont liés à une plateforme, une ABI et des extensions CPU précises — inutilisables ailleurs. La bonne pratique
est de committer le **code qui les produit** (ici : deux petits fichiers, `fetch-sources.sh` et `pcsd.map`) et de les
reconstruire à la compilation. C'est exactement ce que fait ce dossier.

## Ce que fait `fetch-sources.sh`

Clone [vision.cpp](https://github.com/Acly/vision.cpp) et [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp)
(avec leurs sous-modules — chacun embarque sa propre copie de [ggml](https://github.com/ggml-org/ggml)) à un **commit
figé**, dans `depend/visioncpp-src/` et `depend/stablediffusioncpp-src/` — deux dossiers listés dans `.gitignore`,
jamais commités. Le script ne compile rien : c'est `CMakeLists.txt` qui s'en charge ensuite, en détectant simplement
la présence de ces dossiers.

Commits actuellement figés (à mettre à jour à la main dans `fetch-sources.sh` si vous voulez suivre une version plus
récente — testez avant de merger, une mise à jour amont peut changer l'API C) :

| Dépendance | Commit | Date |
|---|---|---|
| vision.cpp | `26a752912d49f6c4ff4545b35a1bdf7400d349ed` | 2026-03-31 |
| stable-diffusion.cpp | `3f8527a46c54ecf4cb4ed6003da8e8982283c73c` | 2026-09-27 |

Prérequis : `git`, une connexion réseau, et environ **1 Go** d'espace disque libre pour les deux sources (avant
compilation — les répertoires de build ajoutent plusieurs centaines de Mo de plus).

## Ce que fait `CMakeLists.txt` avec ces sources

**vision.cpp** est lié **statiquement** dans `photoclone_lib`, via un simple `add_subdirectory(depend/visioncpp-src)`
— comme n'importe quelle dépendance CMake classique. Avant cet appel, le `CMakeLists.txt` force en cache les options
du sous-projet dont PhotoClone a besoin :
- `BUILD_SHARED_LIBS=OFF`, `VISP_STATIC_GGML=ON` : bibliothèques statiques (plus simple à distribuer : un seul
  exécutable, pas de `.so` à côté).
- `GGML_NATIVE=OFF` + `GGML_SSE42/AVX/AVX2/F16C/BMI2/FMA=ON` : base d'instructions **portable** x86_64 (~Haswell,
  2013+). Sans ce réglage, ggml compile avec `-march=native`, c'est-à-dire pour LE PROCESSEUR DE LA MACHINE DE
  COMPILATION précisément — l'exécutable plante avec « instruction illégale » sur toute autre machine plus ancienne.
  À adapter si vous compilez uniquement pour votre propre machine (voir les options `GGML_*` de vision.cpp/ggml) ou
  si vous voulez du GPU (`VISP_VULKAN=ON`, à activer avant l'appel à `add_subdirectory`).
- `VISP_TESTS=OFF` : pas besoin de compiler la suite de tests de vision.cpp.

**stable-diffusion.cpp** est compilé de façon isolée, PAS avec `add_subdirectory` — et c'est délibéré. Il embarque
**sa propre copie de ggml**, dans une version différente de celle de vision.cpp ; les deux liés statiquement dans le
même binaire provoqueraient des symboles ggml en double (erreurs de l'éditeur de liens, ou pire : un mélange
silencieux des deux versions à l'exécution). La solution : le compiler séparément (piloté par `ExternalProject_Add`,
avec les mêmes options `GGML_*` portables que ci-dessus), puis relier son résultat en une **bibliothèque partagée
isolée**, `depend/stablediffusioncpp/lib/libpcsd.so`, avec un **script de version du lieur**
(`depend/stablediffusioncpp/pcsd.map` — un petit fichier texte, à nous, committé) qui ne laisse passer que la
vingtaine de fonctions de l'API C dont PhotoClone a besoin (`new_sd_ctx`, `generate_image`, `sd_cancel_generation`…)
et **aucun symbole ggml** :

```bash
nm -D --defined-only depend/stablediffusioncpp/lib/libpcsd.so | grep ggml   # ne doit rien afficher
```

PhotoClone charge ensuite `libpcsd.so` à **l'exécution** (`dlopen`/`QLibrary`, voir `src/ai/SdBackend.cpp`), jamais au
moment de l'édition de liens : elle ne touche donc jamais au ggml de vision.cpp lié statiquement dans le même
exécutable, quelle que soit sa version. `add_dependencies(photoclone_lib pcsd_shared)` n'existe que pour l'ordre de
compilation (s'assurer que `libpcsd.so` est prêt avant de lancer PhotoClone) — ce n'est pas une édition de liens.

Si vous packagez PhotoClone, `libpcsd.so` doit être installé À CÔTÉ de l'exécutable (ou dans un chemin que le
chargeur dynamique trouve) : `cmake --install` s'en charge (`install(FILES ... DESTINATION lib)`).

## Portabilité et limites de ce qui est compilé

- x86_64 Linux uniquement (les flags `GGML_*` ci-dessus sont pour cette architecture ; sur ARM ou macOS, adaptez
  ou passez par `GGML_NATIVE=ON` si vous ne distribuez pas le binaire ailleurs).
- CPU seulement par défaut. GPU : `VISP_VULKAN=ON` pour vision.cpp avant l'`add_subdirectory` ; pour
  stable-diffusion.cpp, ajoutez `-DSD_CUDA=ON` / `-DSD_VULKAN=ON` / `-DSD_METAL=ON` / `-DSD_HIPBLAS=ON` aux
  `CMAKE_ARGS` de l'`ExternalProject_Add` selon votre matériel.
- **Compilation gourmande en mémoire** : sur une machine à RAM limitée (≲ 4 Go), la compilation parallèle de
  stable-diffusion.cpp peut faire tuer le compilateur par le noyau (« Killed », `cc1plus` OOM) sur certains fichiers
  volumineux (les tables de tokenizer, notamment). Le `BUILD_COMMAND` de l'`ExternalProject_Add` est donc réglé en
  **`--parallel 1`** par défaut pour cette dépendance précise (vision.cpp et PhotoClone lui-même compilent en
  parallèle normalement). Si vous avez plusieurs Go de marge, augmentez ce nombre dans `CMakeLists.txt` pour accélérer.
- Aucun poids de modèle n'est téléchargé ici : voir le README principal, section **Stable Diffusion**, pour
  configurer les vôtres.

## Licences

vision.cpp et stable-diffusion.cpp sont chacun sous licence MIT (voir `LICENSE` dans ce dossier pour chacun — copiées
depuis leur dépôt respectif au moment du dernier commit figé ci-dessus ; vérifiez la licence actuelle en amont si
vous mettez à jour le pin). Les modèles eux-mêmes (poids IA) ont chacun leur propre licence, distincte du code : voir
le README principal.
