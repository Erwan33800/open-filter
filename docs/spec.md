# Open Filter — Cahier des charges

Oct 30, 2026 · @Erwan

## Vision et périmètre

Open Filter est un égaliseur paramétrique et dynamique pour Ableton Live. Il est pensé pour le mixage dub et house, et s'inspire de l'ergonomie de FabFilter Pro-Q 4 : on règle tout directement sur la courbe, et les fonctions avancées ne s'affichent que quand on s'en sert.

**Utilisateur cible** : producteur et performer live sous Ableton, qui mixe des kicks, des basses et des voix et veut corriger vite sans changer d'outil.

**Promesse** : un son transparent par défaut, une courbe lisible d'un coup d'œil, et une dynamique par bande assez simple pour s'en servir en live.

**Base validée** : un premier prototype web (16 bandes, 7 types de filtres, analyseur pré/post, EQ dynamique par bande) a validé l'ergonomie. Il reste consultable dans `maquette/docs/prototype.html` (brouillon, non versionné, à supprimer en fin de projet). L'interface réelle est refaite aux couleurs d'Open Groove (voir `CLAUDE.md`).

**Hors périmètre** (fonctions de Pro-Q 4 écartées) :

- Surround et Dolby Atmos
- Liste d'instances multi-plugins (pilotage des autres plugins FabFilter depuis une seule fenêtre)
- Format AAX / Pro Tools et version iOS
- Prise en charge des surfaces de contrôle Pro Tools

## Formats et plateformes

Open Filter est développé en C++ avec JUCE et livré en VST3 sur Windows et macOS, plus AU sur macOS. C'est ce qu'Ableton Live 11 et 12 chargent nativement.

| Exigence | Valeur | Priorité |
| --- | --- | --- |
| Formats | VST3 (Win + Mac), AU (Mac) | MVP |
| Format CLAP | Via l'extension clap-juce-extensions | v2 |
| Systèmes | Windows 10/11 64 bits, macOS 11+ (Intel et Apple Silicon, binaire universel) | MVP |
| Fréquences d'échantillonnage | 44,1 à 192 kHz | MVP |
| Canaux | Mono et stéréo, plus une entrée sidechain stéréo | MVP |
| Latence | 0 échantillon en mode Zéro latence ; latence reportée à l'hôte dans les autres modes | MVP |
| Précision interne | Calcul en double (64 bits) dans les filtres | MVP |

**Plateforme prioritaire : Windows.** Tes sessions Ableton tournent sous Windows, donc le développement et les tests du MVP se font en VST3 sous Windows 10/11 (compilation avec Visual Studio 2022). Le build macOS (VST3 + AU) reste dans le périmètre, mais il n'est validé qu'avant chaque version, sur un Mac emprunté ou une machine d'intégration continue.

## Bandes et types de filtres

Open Filter offre 24 bandes, comme Pro-Q 4. Chaque bande garde son numéro à vie, même quand on en supprime une autre, pour que l'automation enregistrée dans Ableton pilote toujours la bonne bande.

**Paramètres d'une bande**

| Paramètre | Plage | Défaut |
| --- | --- | --- |
| Fréquence | 10 Hz à 30 kHz, échelle logarithmique | Selon l'endroit du clic |
| Gain | -30 à +30 dB | 0 dB |
| Q | 0,025 à 40 | 1 |
| Pente | 6 à 96 dB/oct par pas de 6, plus Brickwall pour les coupe-bas et coupe-haut | 12 dB/oct |
| Placement stéréo | Stéréo, Gauche, Droite, Mid, Side | Stéréo |
| Activation | Marche / bypass | Marche |

**Types de filtres**

| Type | Gain | Q | Dynamique possible | Priorité |
| --- | --- | --- | --- | --- |
| Cloche | Oui | Oui | Oui | MVP |
| Plateau grave | Oui | Oui (forme du plateau) | Oui | MVP |
| Plateau aigu | Oui | Oui (forme du plateau) | Oui | MVP |
| Coupe-bas | Non | Résonance | Non | MVP |
| Coupe-haut | Non | Résonance | Non | MVP |
| Notch | Non | Oui | Non | MVP |
| Passe-bande | Non | Oui | Non | MVP |
| Tilt (plateau ou plat) | Oui | Non | Oui | v1 |
| Passe-tout (phase seule) | Non | Oui | Non | v2 |

**Règles de calcul**

- Les filtres dérivent des formules du RBJ Audio EQ Cookbook, comme dans le prototype. Les pentes raides cascadent plusieurs biquads.
- Les paramètres sont lissés sur environ 20 ms pour éviter les clics quand on tourne un bouton ou qu'une automation joue.
- Pentes fractionnaires (par exemple 18,5 dB/oct) : v2. Interaction gain–Q façon console analogique : v2.

## EQ dynamique

Toute cloche, tout plateau et tout tilt peut devenir dynamique : son gain bouge selon le niveau d'un signal de détection. Le prototype v0.2 a validé le principe ; le plugin le calcule échantillon par échantillon au lieu de toutes les 10 ms.

**Paramètres dynamiques**

| Paramètre | Plage | Défaut | Priorité |
| --- | --- | --- | --- |
| Range | -30 à +30 dB (négatif = compression, positif = expansion) | 0 dB = bande statique | MVP |
| Seuil | -60 à 0 dB, ou Auto | Auto | MVP |
| Ratio | 1:1 à ∞:1 | 4:1 | MVP |
| Attaque | 0,1 à 200 ms, ou Auto | Auto | MVP |
| Relâchement | 10 à 2000 ms, ou Auto | Auto | MVP |
| Knee (genou) | Doux, 6 dB, fixe | Fixe | MVP |
| Source de détection | Entrée du plugin ou sidechain externe | Entrée | MVP |
| Filtre de détection | Band (suit la bande) ou Libre (coupe-bas et coupe-haut réglables) | Band | v1 |
| Écoute de la détection | Bouton maintenu (casque) | Off | MVP |

**Comportement attendu**

- Mode Auto par défaut : seuil, attaque et relâchement s'adaptent au signal, comme dans Pro-Q 4. Les réglages manuels n'apparaissent qu'en dépliant le panneau dynamique.
- On crée une bande dynamique en tirant sa tige de range sur la courbe, avec Alt + molette, ou avec Alt + double-clic.
- La courbe montre la zone entre gain statique et gain + range, plus la position du gain en temps réel (déjà dans le prototype).
- Le sidechain externe passe par l'entrée sidechain VST3 qu'Ableton expose dans le menu Sidechain du device. Cas d'usage principal : le kick d'une autre piste creuse la basse à 60–100 Hz.
- Dynamique spectrale (déclenchement fréquence par fréquence à l'intérieur de la bande, façon Pro-Q 4) : v2, car elle impose le traitement en phase linéaire.

## Stéréo et placement

Chaque bande choisit les canaux qu'elle traite : stéréo (les deux), gauche, droite, mid ou side. C'est l'outil clé pour un bas du spectre mono et propre en dub et en house.

- **MVP** : placement Stéréo, Mid et Side par bande. Exemple type : coupe-bas à 120 Hz sur le Side pour garder le sub en mono.
- **v1** : placement Gauche et Droite, et bouton « Scinder » qui duplique une bande en paire L/R ou M/S.
- Code couleur des courbes repris de Pro-Q 4 : jaune pour stéréo, blanc pour gauche, rouge pour droite, vert pour mid, bleu pour side.
- Sur une piste mono, le placement est masqué et toutes les bandes traitent le signal unique.

## Modes de phase et qualité

Le MVP n'a qu'un mode, Zéro latence. C'est celui qu'on utilise en live et qui suffit au mixage dans la plupart des cas.

| Mode | Principe | Latence | Priorité |
| --- | --- | --- | --- |
| Zéro latence | Biquads avec correction de la déformation près de Nyquist (courbe plus proche de l'analogique que le bilinéaire classique) | 0 | MVP |
| Phase naturelle | Magnitude et phase proches de l'analogique, sans pré-écho audible | Faible, reportée à l'hôte | v2 |
| Phase linéaire | Filtres FIR par FFT, 3 résolutions (Basse, Moyenne, Haute) | Environ 70 à 210 ms à 44,1 kHz selon la résolution | v2 |

**Point d'attention** : un biquad classique « écrase » les cloches près de 20 kHz (le manuel de Pro-Q 4 montre l'écart sur une cloche à 15 kHz). Le MVP corrige ça par des formules adaptées (méthode de Vicanek, étape 2 faite) : sur une cloche à 15 kHz (+12 dB, Q = 1, 44,1 kHz), l'écart avec l'analogique tombe de 7,5 dB (RBJ) à environ 1 dB. Un filtre numérique d'ordre 2 ne peut pas suivre exactement l'analogique près de Nyquist : pour aller plus loin il faudrait le suréchantillonnage ou les modes Phase naturelle / linéaire (v2).

Les modes de caractère (saturation Subtle et Warm de Pro-Q 4) sont à discuter pour la v2 : ils colleraient au son dub, mais ne font pas partie du cœur d'un EQ.

## Analyseur de spectre

L'analyseur affiche en temps réel le spectre avant EQ, après EQ et du sidechain, derrière la courbe. Il tourne dans le thread de l'interface, jamais dans le thread audio.

| Réglage | Valeurs | Défaut | Priorité |
| --- | --- | --- | --- |
| Spectres affichés | Pré, Post, Sidechain (combinables) | Pré + Post | MVP (Sidechain : v1) |
| Résolution FFT | 2048, 4096, 8192 points | 4096 | MVP |
| Vitesse de retombée | Lente, Moyenne, Rapide | Moyenne | MVP |
| Inclinaison d'affichage | 0 à 6 dB/oct autour de 1 kHz | 4,5 dB/oct | MVP |
| Plage verticale | 60, 90, 120 dB | 90 dB | MVP |
| Freeze (maintien des pics) | Marche / arrêt | Arrêt | v1 |
| Spectrum Grab | Attraper un pic du spectre pour créer une cloche dessus | Activé | v1 |
| Collisions | Surligner les zones où le sidechain masque le signal | Arrêt | v2 |

Le Spectrum Grab est la fonction la plus utile à ajouter au prototype ensuite : on laisse la souris sur le spectre, les pics s'étiquettent avec leur fréquence, et on tire un pic vers le bas pour le corriger.

## Interface et interactions

L'écran de courbe occupe toute la fenêtre. Le panneau de la bande sélectionnée flotte sous son nœud, et la barre du bas porte les réglages globaux.

**Gestes sur la courbe**

| Geste | Action | Priorité |
| --- | --- | --- |
| Double-clic sur le fond | Créer une bande ; le type dépend de la position (coupe-bas tout à gauche, plateau près des bords, cloche ailleurs) | MVP |
| Glisser un nœud | Fréquence (horizontal) et gain (vertical) | MVP |
| Molette sur un nœud | Q ; pente pour les coupe-bas et coupe-haut | MVP |
| Maj + geste | Réglage fin | MVP |
| Alt + clic sur un nœud | Bypass de la bande | MVP |
| Clic droit sur un nœud | Menu : type, placement, rendre dynamique, copier, supprimer | MVP |
| Glisser la tige de range | Range dynamique | MVP |
| Rectangle sur le fond, Ctrl/Cmd + clic | Sélection multiple, réglage groupé | v1 |
| Maintenir le bouton casque | Solo de la bande (on n'entend que la zone concernée) | MVP |
| Glisser l'échelle des fréquences | Zoom horizontal | v2 |
| EQ Sketch | Dessiner la courbe d'un geste, les bandes se créent seules | v2 |

**Saisie et affichage**

- Double-clic sur une valeur pour la taper : « 2k », « A4 » ou « C#2+13 » pour une fréquence, « 2x » pour +6 dB.
- Les notes suivent la convention d'Ableton (C3 = do central), déjà en place dans le prototype. Pro-Q 4 utilise C4 : c'est un choix assumé pour coller à Live.
- Échelle de gain ± 3, 6, 12 ou 30 dB, qui s'élargit seule quand on dépasse.
- Fenêtre redimensionnable librement en VST3, plus 3 tailles prédéfinies et un mode plein écran (v1).
- Interface en HTML/CSS/JS dans un WebView2 (comme Open Groove, mêmes variables CSS et même police). Le canvas 2D suffit pour 60 images/s ; la FFT reste en C++ et n'envoie à la page qu'un spectre réduit (quelques centaines de points, environ 30 fois par seconde).
- Affichage clavier de piano sous la courbe pour caler les bandes sur des notes : v2.

## Fonctions globales

Les réglages globaux tiennent dans la barre du bas et dans l'en-tête.

| Fonction | Détail | Priorité |
| --- | --- | --- |
| Gain de sortie | -∞ à +36 dB, sans écrêtage interne (calcul en virgule flottante) | MVP |
| Bypass global | Sans clic, avec fondu court ; latence compensée dans les modes à latence | MVP |
| Inversion de phase | Bouton sur la sortie | MVP |
| Vumètre de sortie | Crête et RMS, indicateur de dépassement de 0 dBFS | MVP |
| Auto-gain | Compense le volume ajouté ou retiré par les bandes, estimé à partir de la courbe | v1 |
| Échelle de gain | Multiplie le gain de toutes les bandes (0 à 200 %), pratique à automatiser | v1 |
| Annuler / rétablir | Historique des changements faits à la souris (pas ceux de l'automation) | MVP |
| Comparaison A/B | Deux états, bouton Copier A vers B | v1 |
| Presets d'usine | 8 familles (Kick, Bass, Lead, Vocal, Drums, Pad, Master, Guitare), 2 à 4 variantes chacune (vintage, moderne, dub, house…), avec leurs macros | MVP |
| Presets utilisateur | Enregistrer, charger, fichiers .openfilter lisibles, glisser-déposer | v2 |
| Copier-coller de bandes | Entre deux instances de Open Filter | v1 |
| Panoramique de sortie | L/R ou M/S | v2 |

## Intégration Ableton

Tout paramètre de Open Filter est automatisable et mappable depuis Live, sans MIDI Learn interne : on passe par le mode Configure du device et le mapping MIDI d'Ableton (Ctrl/Cmd + M). C'est plus fiable, car Live n'envoie le MIDI qu'au premier plugin d'une piste.

**Paramètres exposés à l'hôte**

- Par bande (24 jeux fixes) : activée, type, fréquence, gain, Q, pente, placement, range dynamique, seuil, attaque, relâchement.
- Globaux : gain de sortie, bypass, échelle de gain, inversion de phase.
- Noms lisibles dans Live : « B3 Freq », « B3 Gain », « B3 Dyn Range », avec valeurs affichées en Hz et en dB.
- Les paramètres d'une bande gardent leur index même quand la bande est supprimée puis recréée, pour ne jamais casser une automation.

**Usage live (APC40 MK2, MIDImix envisagé)**

- Prévoir 8 macros fixes en plus des paramètres de bande, assignables à plusieurs paramètres avec une plage chacun. Exemple : une macro « Filtre dub » qui ferme le coupe-haut de 20 kHz à 800 Hz en montant sa résonance. C'est le geste type d'un live dub, et un seul potard de l'APC40 suffit à le jouer.
- Les macros sont v1. En attendant, un Audio Effect Rack autour de Open Filter fait le même travail avec les macros de Live.

**Sidechain**

- Entrée sidechain stéréo VST3 / AU, choisie dans le menu Sidechain du device dans Live (Live 10.1 et suivants).
- Si rien n'arrive sur le sidechain alors qu'une bande l'utilise, le plugin l'affiche clairement au lieu de rester muet.

## Exigences non fonctionnelles

Ces objectifs se mesurent sur un projet de test fixe, à 44,1 kHz avec un buffer de 128 échantillons.

| Exigence | Objectif mesurable |
| --- | --- |
| CPU, 8 bandes statiques | Moins de 1 % d'un cœur (stéréo, mode Zéro latence) |
| CPU, 24 bandes dont 8 dynamiques | Moins de 3 % d'un cœur |
| Thread audio | Aucune allocation mémoire, aucun verrou, aucun appel système |
| Bruit et précision | Bande à 0 dB ou bypassée : sortie identique à l'entrée au bit près |
| Fidélité de courbe | Écart de moins de 0,1 dB avec la courbe analogique pour une cloche (Q ≥ 1, fréquence centrale ≤ 1 kHz) et de moins de 0,2 dB pour les plateaux (fréquence ≤ 1 kHz), de 20 Hz à 20 kHz. Moins de 1,5 dB pour une cloche à 15 kHz à 44,1 kHz. Tenu par les tests du DSP |
| Automation | Aucun clic audible sur un balayage de fréquence de 20 Hz à 20 kHz en 1 s |
| Stabilité | Aucun NaN ni débordement, même avec Q = 40, gain +30 dB et changement brutal de fréquence d'échantillonnage |
| Sauvegarde | Un projet Live rouvert restaure exactement l'état, y compris les bandes dynamiques et la taille de fenêtre |
| Interface | 60 images/s avec analyseur actif, ouverture de la fenêtre en moins de 300 ms |

**Tests**

- Tests unitaires du DSP : réponse de chaque type de filtre comparée à la référence, comportement de la détection dynamique sur des signaux synthétiques (sinus en rafales, bruit rose).
- Validation des formats avec pluginval (niveau de rigueur 10) et l'auval d'Apple pour l'AU.
- Test d'écoute A/B en aveugle contre l'EQ Eight d'Ableton sur des réglages identiques.

## Feuille de route

Le prototype web (fait) a validé l'ergonomie. Chaque version démarre quand le critère sous la précédente est atteint.

| Version | Contenu | Critère de passage |
| --- | --- | --- |
| MVP | 24 bandes, 7 types ; mode zéro latence fidèle ; dynamique par bande ; sidechain externe ; stéréo, Mid et Side ; analyseur pré et post ; undo, presets d'usine, VST3 et AU | Passe pluginval et tient un live complet |
| v1 | Tilt, placements L et R ; détection à filtre libre ; Spectrum Grab et Freeze ; sélection multiple, A/B ; auto-gain, échelle de gain ; 8 macros pour l'APC40 (déjà maquettées) | Remplace EQ Eight sur tes sessions |
| v2 | Phase naturelle et linéaire ; dynamique spectrale ; EQ Sketch, clavier piano ; zoom et collisions ; pentes fractionnaires ; caractère (à décider) ; presets utilisateur ; format CLAP | Selon tes retours d'usage |

La phase linéaire et la dynamique spectrale restent en v2 : elles demandent un moteur FFT à part et ajoutent de la latence, inutile en live.
