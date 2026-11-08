# Open Filter

Égaliseur paramétrique et dynamique (VST3, AU sur Mac) pour Ableton Live, dans l'esprit de FabFilter Pro-Q : on règle tout directement sur la courbe, les fonctions avancées n'apparaissent que quand on s'en sert.

> **État : étape 1 (squelette).** Le plugin se charge dans Live, affiche l'interface et laisse passer le son sans le modifier. Le traitement audio n'est pas encore branché (voir `docs/spec.md` et `CLAUDE.md`).

## 1. Télécharger

Le dépôt ne contient que le code source. Le plugin prêt à installer est fabriqué automatiquement par GitHub et publié sur la page **Releases** :

1. Ouvre la page **[Releases](../../releases/latest)**.
2. Télécharge **`OpenFilter-Windows.zip`** (ou `OpenFilter-macOS.zip`), puis clic droit > **Extraire tout**.

## 2. Installer (Windows)

1. Copie le dossier **`Open Filter.vst3`** dans `C:\Program Files\Common Files\VST3` (Windows demande une confirmation administrateur).
2. Dans Live : **Options > Préférences > Plug-ins**, active **Utiliser les dossiers système VST3**, puis **Réanalyser**.
3. Open Filter apparaît dans **Plug-ins > VST3 > Erwan > Open Filter**, à placer sur une piste audio.

**Mettre à jour :** ferme Live, remplace le dossier `Open Filter.vst3`, rouvre Live.

## 3. Développer

- Code commun : Open Filter s'appuie sur Open Core (dépôt `open-core` : CMake, éditeur WebView2, CSS et pont communs, CI), à la version indiquée dans `OPEN_CORE_VERSION`. En local, place le dossier `open-core` à côté de celui-ci (ou définis `OPEN_CORE_DIR`).
- Interface : modifier `ui/src/*`, puis `python ui/build_ui.py`, et commiter aussi `ui/openfilter_ui.html`. Ouvrir ce fichier dans Chrome suffit pour tester l'interface (données factices).
- Tests du DSP seuls (WSL ou Linux, sans JUCE) :
  ```
  cmake -B build-tests -DOPENFILTER_PLUGIN=OFF
  cmake --build build-tests
  ctest --test-dir build-tests --output-on-failure
  ```
- Plugin complet : voir `CLAUDE.md` (Visual Studio 2022 et WebView2 sous Windows, sinon la CI GitHub compile à chaque push).

Licences : JUCE 8 (Starter / AGPLv3), SDK VST3 (MIT), police Manrope (SIL OFL), Catch2 (BSL-1.0).
