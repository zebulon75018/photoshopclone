#!/usr/bin/env bash
# Récupère les sources upstream des deux dépendances IA optionnelles de PhotoClone, à un commit précis (figé pour
# une compilation reproductible), dans depend/visioncpp-src et depend/stablediffusioncpp-src. Ces deux dossiers ne
# sont PAS commités dans ce dépôt (voir .gitignore) : c'est CMake, au moment de la compilation, qui les construit
# à partir de ces sources (voir CMakeLists.txt et depend/README.md pour le détail et les raisons de ce choix).
#
# Usage :
#   ./depend/fetch-sources.sh              récupère ce qui manque (ne retélécharge pas ce qui est déjà là)
#   ./depend/fetch-sources.sh --force      supprime et retélécharge les deux, même si déjà présents
#   ./depend/fetch-sources.sh visioncpp    récupère uniquement vision.cpp
#   ./depend/fetch-sources.sh sdcpp        récupère uniquement stable-diffusion.cpp
#
# Nécessite : git, et environ 1 Go d'espace disque libre (les deux dépôts embarquent ggml en sous-module chacun,
# avec leur propre historique). Rien n'est compilé ici — seule la compilation (cmake --build) le fait.
set -euo pipefail

VISIONCPP_URL="https://github.com/Acly/vision.cpp.git"
VISIONCPP_COMMIT="26a752912d49f6c4ff4545b35a1bdf7400d349ed"   # 2026-03-31 — voir depend/README.md pour mettre à jour ce pin

SDCPP_URL="https://github.com/leejet/stable-diffusion.cpp.git"
SDCPP_COMMIT="3f8527a46c54ecf4cb4ed6003da8e8982283c73c"        # 2026-09-27 — voir depend/README.md pour mettre à jour ce pin

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FORCE=0
WHAT="all"
for arg in "$@"; do
  case "$arg" in
    --force|-f) FORCE=1 ;;
    visioncpp)  WHAT="visioncpp" ;;
    sdcpp)      WHAT="sdcpp" ;;
    -h|--help)
      sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "Argument inconnu : $arg (voir --help)" >&2; exit 1 ;;
  esac
done

if ! command -v git >/dev/null 2>&1; then
  echo "git est requis mais introuvable dans le PATH." >&2
  exit 1
fi

fetch() {
  local name="$1" url="$2" commit="$3" dest="$SCRIPT_DIR/$4"
  if [ -d "$dest" ] && [ "$FORCE" -eq 0 ]; then
    echo "[$name] déjà présent dans $dest (utilisez --force pour retélécharger) : ignoré."
    return 0
  fi
  if [ -d "$dest" ]; then
    echo "[$name] --force : suppression de $dest…"
    rm -rf "$dest"
  fi
  echo "[$name] clonage de $url…"
  git clone --quiet --recursive "$url" "$dest"
  echo "[$name] extraction du commit figé $commit…"
  git -C "$dest" checkout --quiet --recurse-submodules "$commit"
  local head
  head="$(git -C "$dest" rev-parse HEAD)"
  if [ "$head" != "$commit" ]; then
    echo "[$name] ERREUR : commit obtenu ($head) différent du commit attendu ($commit)." >&2
    exit 1
  fi
  echo "[$name] prêt ($(du -sh "$dest" | cut -f1))."
}

if [ "$WHAT" = "all" ] || [ "$WHAT" = "visioncpp" ]; then
  fetch "vision.cpp" "$VISIONCPP_URL" "$VISIONCPP_COMMIT" "visioncpp-src"
fi
if [ "$WHAT" = "all" ] || [ "$WHAT" = "sdcpp" ]; then
  fetch "stable-diffusion.cpp" "$SDCPP_URL" "$SDCPP_COMMIT" "stablediffusioncpp-src"
fi

cat <<'EOF'

Terminé. Étape suivante : configurez et compilez PhotoClone normalement —
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
CMake détecte automatiquement ces sources et compile vision.cpp et stable-diffusion.cpp au passage
(la première compilation est longue : plusieurs minutes, ggml compile beaucoup de fichiers).
EOF
