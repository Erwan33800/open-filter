# Open Filter : contexte du projet

Lu automatiquement par Claude Code à l'ouverture du dépôt.

## Le projet en une phrase

Open Filter est un égaliseur paramétrique et dynamique (VST3, Windows d'abord, AU ensuite) pour Ableton Live, qui reprend les fonctions que l'on trouve dans les EQ modernes comme FabFilter Pro-Q 4 : on règle tout sur la courbe, les fonctions avancées n'apparaissent que quand on s'en sert. Compagnon d'Open Groove : même famille visuelle, même pile technique.

## L'utilisateur

- Erwan, producteur et DJ (dub, house), Ableton Live 12 Suite, Windows. Il parle **français**.
- Développeur (Product Owner, formation architecture logicielle), pas spécialiste C++ ni audio : expliquer les choix et les contraintes simplement.
- Il veut une UX très travaillée mais simple, une interface « instrument » sombre et plate.

## Références

- Cahier des charges : `docs/spec.md` (plages, défauts, priorités MVP / v1 / v2). On ne développe que ce qui est marqué **MVP**, sauf demande explicite. Les idées hors MVP vont dans `docs/backlog.md`.
- `maquette/` (racine, hors dépôt) : brouillon du POC (ancien nom « Prisme EQ », ancien CLAUDE.md, prototype Web Audio). **Non versionné**, consultable pour le comportement de la courbe (`maquette/docs/prototype.html`), à supprimer quand le projet sera terminé. Ne rien y modifier.
- Projet frère : `../open-groove` (même pile WebView2, mêmes tokens visuels).

## Langue

- Échanges, commentaires, texte de l'interface, messages de commit : en français.
- Identifiants (classes, fonctions, variables, IDs de paramètres) : en anglais.

## Pile technique

- C++20, JUCE 8.0.4 (FetchContent, tag épinglé, jamais une branche), Catch2 v3 pour les tests du DSP.
- **Interface en HTML/CSS/JS dans un WebView2**, comme Open Groove : `ui/src/*` assemblé par `ui/build_ui.py` en `ui/openfilter_ui.html`, embarqué via `juce_add_binary_data`. Ne jamais éditer le HTML généré à la main.
- La page doit aussi fonctionner seule dans Chrome (données factices) pour itérer sans compiler.
- Repli si la fluidité est insuffisante dans Live : rendu natif JUCE (`juce::Path`).

## Identité visuelle (partagée avec Open Groove)

Reprendre les variables de `../open-groove/ui/src/plugin-head.html` sans les réinventer :

- Fond `--bg #151516`, panneaux `--panel #1d1d1f`, tuiles `--tile #2a2a2d` / `--tile-hi #343438`, traits `--line #2c2c30`.
- Texte `--fg #f1f0ee`, `--dim #a4a4aa`, `--faint #6f6f77`.
- Police Manrope (SIL OFL, base64 via `__MANROPE__`), Cascadia Mono pour les chiffres.
- Look plat, tuiles arrondies (8 px), pas de dégradés, **un seul accent** à la fois. L'accent d'Open Filter reste à choisir parmi la palette d'Open Groove (teal `#5fd4b8` par défaut).
- Composants à réutiliser tels quels : en-tête avec logo-marque et onglets en capitales espacées, valeurs à glisser (`.vb`), segments (`.segc`), menus (`.menu`), barre du bas.
- Couleurs de placement stéréo (spec) : stéréo jaune, gauche blanc, droite rouge, mid vert, side bleu. À accorder avec la palette plate.

## Architecture visée

```
CMakeLists.txt
Source/
  dsp/            C++ pur, sans JUCE GUI, testable seul
    Biquad, FilterDesign (correction près de Nyquist), Band, Dynamics, EqEngine
  params/         layout APVTS (24 bandes + globaux), IDs stables
  analyser/       FIFO sans verrou, FFT, lissage
  PluginProcessor.*   effet stéréo + bus sidechain, état
  PluginEditor.*      WebBrowserComponent (WebView2), pont JS <-> C++
ui/
  src/              plugin-head.html (HTML + CSS), app.js, manrope-latin.woff2
  build_ui.py       assemble openfilter_ui.html (police en base64 via __MANROPE__)
  openfilter_ui.html  page générée, embarquée dans le binaire
tests/dsp/          Catch2
scripts/install.ps1
docs/               spec.md, backlog.md
```

### Pont interface <-> plugin (à détailler à l'étape 3)

- Page vers plugin : gestes sur la courbe via `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture`, pour que Live enregistre l'automation proprement.
- Plugin vers page : valeurs des paramètres, spectre réduit (~30 Hz), niveaux du vumètre.
- La réponse en fréquence affichée est calculée en JS avec les mêmes formules que `FilterDesign` ; un test vérifie la concordance.

## Règles du thread audio (non négociables)

- Dans `processBlock` : aucune allocation, aucun verrou, aucune entrée-sortie, aucun log.
- Mémoire préparée dans `prepareToPlay` (24 bandes × 8 étages max).
- Échanges audio <-> interface : atomiques ou `juce::AbstractFifo` uniquement.
- `ScopedNoDenormals` en tête de `processBlock`. Filtres en `double`.

## DSP

- Coefficients RBJ Audio EQ Cookbook, avec correction près de Nyquist (Vicanek, « Matched Second Order Digital Filters »), écart < 0,1 dB jusqu'à 20 kHz à 44,1 kHz.
- Pentes : cascade de Butterworth (6 dB/oct = 1er ordre, puis biquads), résonance sur le dernier étage seulement.
- Paramètres lissés (~20 ms, `SmoothedValue`), coefficients recalculés tous les 32 échantillons.
- Mid/Side : M = (L+R)/2, S = (L−R)/2 ; ne convertir que si une bande active l'utilise.
- Dynamique : détection filtrée, enveloppe attaque/relâchement, genou doux 6 dB, décalage de gain borné par le range, ajouté au gain avant le calcul des coefficients. Sidechain externe optionnel, repli sur l'entrée.
- Bande à 0 dB ou bypassée : sortie identique à l'entrée au bit près.

## Paramètres

- IDs stables `b{n}_{param}`, n de 1 à 24 : `freq gain q type slope place on dynRange thresh ratio attack release used`. Globaux : `out_gain`, `bypass`, `phase_invert`.
- `juce::ParameterID` en version 1. **Ne jamais renommer ni supprimer un ID publié** (casse l'automation dans Live).
- Noms dans Live : « B3 Freq », « B3 Gain »… Fréquence logarithmique 10 Hz à 30 kHz.
- L'état sauvegarde l'APVTS plus les réglages d'interface (taille de fenêtre, analyseur) dans un nœud séparé.
- Notes musicales en convention Ableton : C3 = do central.

## Compiler et tester

Le projet vit dans WSL ; Visual Studio compile mal depuis un chemin `\wsl.localhost\...`. Décision (nov. 2026) : **la CI GitHub compile le plugin Windows et macOS** (comme Open Groove), et **le DSP se teste en local dans WSL**.

- **Tests du DSP seuls (WSL / Linux, sans JUCE, rapide)** :
  ```bash
  cmake -B build-tests -DOPENFILTER_PLUGIN=OFF
  cmake --build build-tests
  ctest --test-dir build-tests --output-on-failure
  ```
  Prérequis WSL : `sudo apt install cmake g++ ninja-build` (absents au 8 nov. 2026). Tout ce qui est dans `Source/dsp/` doit rester compilable ainsi : pas d'include JUCE.
- **Plugin complet** : poussé sur GitHub, la CI (`.github/workflows/build.yml`) lance les tests du DSP, compile Windows (VST3 + Standalone) et macOS (VST3 + AU), exécute pluginval niveau 10 (`--skip-gui-tests`, sans interface sur le runner), puis publie la release « latest » sur `main`.
- **Compilation locale Windows** (optionnelle, il faudrait cloner le dépôt sous `C:\` et installer CMake ≥ 3.25 + Visual Studio 2022 « Développement Desktop en C++ » + SDK WebView2, voir le workflow) :
  `cmake -B build -G "Visual Studio 17 2022" -A x64`, puis `cmake --build build --config Release`.
- **Installer dans Live** : `powershell -ExecutionPolicy Bypass -File scripts\install.ps1` (terminal administrateur), en lui donnant le chemin du `.vst3` dézippé depuis la release.
- **Interface seule** : `python ui/build_ui.py` puis ouvrir `ui/openfilter_ui.html` dans Chrome (données factices). `ui/openfilter_ui.html` est généré mais **versionné** (CMake l'embarque) : modifier `ui/src/*`, régénérer, commiter les deux.

**Noms de tests Catch2 en ASCII** (anglais, sans accent ni apostrophe) : ctest les passe à Catch2 en ligne de commande et la console Windows les déforme, le test échoue alors avec « No test cases matched » (vu à l'étape 1). Les commentaires et les messages restent en français.

Une étape n'est terminée que si : tests du DSP OK, compilation CI OK, pluginval OK, commit.

## Ordre de développement

Chaque étape finit par : compilation OK, tests OK, pluginval OK, un commit, validation d'Erwan dans Live avant la suivante.

0. Spec v2 et maquette HTML de l'interface (aux couleurs d'Open Groove), validées dans Chrome.
1. Squelette : CMake + JUCE + Catch2, effet stéréo passe-plat avec sidechain, WebView2 affichant la page, script d'installation.
2. DSP des filtres, 3. Paramètres (APVTS), 4. Moteur (stéréo, M/S, lissage, bypass), 5. Dynamique et sidechain, 6. Analyseur, 7. Courbe et gestes branchés au plugin, 8. Panneau de bande, barre du bas, undo, presets, 9. Passe finale (CPU, pluginval, live complet).

## Git

- Un commit par étape ou sous-étape, message en français à l'impératif.
- `build/`, `tools/`, `maquette/` et les fichiers Visual Studio sont ignorés.
