# Traductions de PhotoClone

Les chaînes du code source sont en **français** (langue source). Chaque fichier
`photoclone_<code>.ts` ajoute une langue ; il est compilé en `.qm` par `lrelease`
au moment du build et embarqué dans la ressource `:/i18n` de l'exécutable.

La langue est choisie au démarrage par `I18n::install()` : réglage enregistré,
sinon langue du système, avec repli sur l'anglais puis le français.

## Ajouter une langue

1. Mettre à jour les fichiers sources de traduction (depuis la racine du projet) :

   ```
   lupdate -no-obsolete -locations none src -ts translations/photoclone_en.ts translations/photoclone_XX.ts
   ```

2. Traduire `translations/photoclone_XX.ts` avec Qt Linguist (`linguist`) ou un éditeur de texte.
3. Rien d'autre : le build (CMake) détecte le nouveau `.ts`, le compile en `.qm` et l'embarque.

Si `lrelease` est introuvable au moment du build, l'application reste en français.
