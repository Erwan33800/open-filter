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

### Pont interface <-> plugin (fait)

Événement JUCE unique `openfilter`, tableau `[commande, ...arguments]`. Code : `ui/src/bridge.js` (page) et `Source/PluginEditor.cpp` (plugin).

- **Page -> plugin** : `ready` (page chargée) ; `begin <id>` ; `set <id> <valeur>` ; `end <id>` ; `ui <json>` (réglages d'interface à sauvegarder).
- **Plugin -> page** : `snapshot {id: valeur} <json>` (état complet, en réponse à `ready`) ; `params [[id, valeur], ...]` (changements venus de l'hôte).
- Les valeurs sont les **vraies valeurs** des paramètres (Hz, dB, index de choix, 0/1), jamais normalisées : le plugin convertit avec `convertTo0to1`.
- **Gestes d'automation** : pendant un glisser (bouton enfoncé), chaque paramètre touché ouvre un geste (`begin`) qui se referme au relâchement (`end`) ; molette et menus envoient `begin/set/end` d'un coup. Le plugin appelle `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture`. Si la fenêtre se ferme en plein geste, les gestes ouverts sont refermés.
- **Pas d'écho** : le plugin surveille les 419 valeurs atomiques à 30 Hz (pas d'écouteur) et ne renvoie à la page que ce qu'elle ne connaît pas déjà. Avant le `snapshot`, la page ne pousse rien (elle écraserait l'état sauvegardé).
- **Cases fixes** : une bande est identifiée par sa case (1 à 24), comme « B3 » dans Live ; un preset occupe les cases 1 à n et libère les autres.
- **Macros** : les valeurs `macro1` à `macro8` sont des paramètres ; leurs assignations vivent dans l'état d'interface (JSON). Une macro agit en réécrivant les paramètres qu'elle pilote, **donc seulement quand la fenêtre est ouverte** : limite connue, voir `docs/backlog.md` (moteur de macros en C++, modulation par-dessus les valeurs de base).
- Réglages d'interface sauvegardés : échelle de gain, analyseur (spectres, retombée, plage, inclinaison), nom du preset, noms et assignations des macros.
- **Test** : `python tests/ui/bridge_test.py` charge la page dans Chrome avec un faux `window.__JUCE__` (28 vérifications : état restauré, gestes, automation de l'hôte, presets, macros, pas d'écho).
- La réponse en fréquence affichée est encore calculée en JS avec le RBJ simple : à aligner sur `FilterDesign` (étape 7), avec un test de concordance.

## Règles du thread audio (non négociables)

- Dans `processBlock` : aucune allocation, aucun verrou, aucune entrée-sortie, aucun log.
- Mémoire préparée dans `prepareToPlay` (24 bandes × 8 étages max).
- Échanges audio <-> interface : atomiques ou `juce::AbstractFifo` uniquement.
- `ScopedNoDenormals` en tête de `processBlock`. Filtres en `double`.

## DSP

- Coefficients (`Source/dsp/FilterDesign.cpp`, étape 2 faite) : prototypes analogiques du RBJ Audio EQ Cookbook, ramenés en numérique par la méthode « matched » de Vicanek (pôles par exp(sT), numérateur ajusté pour retrouver le gain analogique à 0 Hz, à la fréquence centrale et à Nyquist). Une atténuation est l'inverse exact de l'amplification de même réglage ; plateau aigu = gain linéaire × plateau grave inversé. Coupe-haut et passe-bande en « matched », coupe-bas et notch en RBJ. Repli automatique sur le RBJ si le calcul est instable ou impossible (jamais de NaN ni d'instabilité, vérifié par balayage).
- Précision mesurée contre l'analogique, de 20 Hz à 20 kHz : cloche < 0,1 dB (Q ≥ 1, f0 ≤ 1 kHz), plateaux < 0,2 dB (f0 ≤ 1 kHz), cloche à 15 kHz < 1,5 dB à 44,1 kHz (RBJ : 7,5 dB). Les écarts croissent avec la fréquence centrale et quand Q est faible : limite d'un filtre numérique d'ordre 2.
- L'interface (`ui/src/app.js`) calcule encore la courbe avec le RBJ simple : à aligner sur `FilterDesign` à l'étape 7, avec un test de concordance.
- Pentes : cascade de Butterworth (6 dB/oct = 1er ordre, puis biquads), résonance sur le dernier étage seulement.
- Paramètres lissés (~20 ms, `SmoothedValue`), coefficients recalculés tous les 32 échantillons.
- Mid/Side : M = (L+R)/2, S = (L−R)/2 ; ne convertir que si une bande active l'utilise.
- Dynamique : détection filtrée, enveloppe attaque/relâchement, genou doux 6 dB, décalage de gain borné par le range, ajouté au gain avant le calcul des coefficients. Sidechain externe optionnel, repli sur l'entrée.
- Bande à 0 dB ou bypassée : sortie identique à l'entrée au bit près.

## Paramètres

- Définition unique dans `Source/params/ParamSpec.cpp` (C++ pur, testé) ; `ParameterLayout.cpp` en construit l'APVTS. 419 paramètres : 24 bandes x 17, 3 globaux, 8 macros.
- IDs stables `b{n}_{param}`, n de 1 à 24 : `freq gain q type slope place on dynRange thresh threshAuto ratio attack attackAuto release releaseAuto dynSrc used`. Globaux : `out_gain`, `bypass`, `phase_invert`. Macros : `macro1` à `macro8` (0 à 1) ; leurs assignations (quel réglage, de quelle valeur à quelle valeur) ne sont pas des paramètres : elles vivent dans l'état d'interface.
- `used`, `threshAuto`, `attackAuto`, `releaseAuto` ne sont pas automatisables (ils restent sauvegardés).
- Les choix `type`, `slope`, `place` listent déjà les valeurs réservées pour plus tard (Tilt, All Pass, Brickwall, Left, Right) : un choix ajouté au milieu décalerait les valeurs automatisées. Toute nouvelle valeur s'ajoute à la fin. Un test gèle les identifiants et les listes.
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

## Écouter et mesurer sans Live : `of_render`

Outil en ligne de commande qui applique l'égaliseur à un fichier WAV avec exactement le même code C++ que le plugin (`Source/dsp/EqEngine`). Compilé avec les tests (`cmake --build build-tests`).

```bash
# signaux intégrés : gen:sweep (balayage 20 Hz - 20 kHz, 8 s) et gen:pink (bruit rose, 10 s), ou un WAV (16/24/32 bits, float)
./build-tests/of_render gen:sweep out/sweep_eq.wav --band lowcut,80,0,0.71,24 --band bell,1000,8,2 --gain -2 --response out/courbe.csv
# bande : type,freq,gain,q[,pente[,placement]]  (types : bell lowshelf highshelf lowcut highcut notch bandpass ; placements : stereo left right mid side)
```

Le WAV de sortie est en float 32 bits (pas d'écrêtage). `--response` écrit la courbe numérique et la référence analogique. Les fichiers générés vont dans `out/` (ignoré par git), visible depuis Windows dans `\\wsl.localhost\Ubuntu\home\erwan_lp\projects\open-plugin\open-filter\out`.

## Moteur (`Source/dsp/EqEngine`, étape 4 faite)

- 24 bandes en série, tout en `double` (conversion float -> double au début du bloc, retour à la fin).
- Lissage ~20 ms : fréquence et Q en échelle log, gain linéaire ; les coefficients se recalculent tous les 32 échantillons, seulement si une valeur a bougé. Un changement de type, de pente ou de placement s'applique d'un coup (états remis à zéro) : pas de fondu, à améliorer si l'automation de ces réglages pose problème.
- Activer, désactiver ou supprimer une bande, et le bypass global : fondu de 20 ms. Bypass complet = sortie identique à l'entrée au bit près, états remis à zéro.
- Mid/Side : conversion en `double`, seulement si une bande active utilise Mid ou Side (sinon le tampon n'est pas touché). Left et Right sont déjà gérés par le moteur (réservés dans l'interface, v1). Sur une piste mono, toutes les bandes traitent l'unique canal.
- Rien d'actif, gain de sortie à 0 dB, pas de bypass : le tampon n'est pas modifié (bit exact).
- Gain de sortie (avec l'inversion de phase) lissé échantillon par échantillon.
- Le processeur JUCE ne fait que lire les paramètres atomiques et les passer au moteur (`pushParametersToEngine`). Les macros, la dynamique et le sidechain ne sont pas encore branchés (étape 5 et interface). Les types réservés Tilt et All Pass mettent la bande hors service.

## Ordre de développement

Chaque étape finit par : compilation OK, tests OK, pluginval OK, un commit, validation d'Erwan dans Live avant la suivante.

0. Spec v2 et maquette HTML de l'interface (aux couleurs d'Open Groove), validées dans Chrome.
1. Squelette : CMake + JUCE + Catch2, effet stéréo passe-plat avec sidechain, WebView2 affichant la page, script d'installation.
2. DSP des filtres (fait), 3. Paramètres (APVTS) (fait), 4. Moteur (stéréo, M/S, lissage, bypass) (fait), 5. Dynamique et sidechain, 6. Analyseur, 7. Courbe et gestes branchés au plugin, 8. Panneau de bande, barre du bas, undo, presets, 9. Passe finale (CPU, pluginval, live complet).

## Git

- Un commit par étape ou sous-étape, message en français à l'impératif.
- `build/`, `tools/`, `maquette/` et les fichiers Visual Studio sont ignorés.
