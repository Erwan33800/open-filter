# Backlog

Idées hors MVP, notées pendant le développement. On les reprend au moment de la v1 ou de la v2.

- Presets utilisateur : enregistrer, charger et partager des fichiers `.openfilter` (décidé v2, 8 nov. 2026).
- Macros : modulation « par-dessus » les valeurs de base plutôt que de les écraser, pour que l'automation des réglages sous-jacents reste valable dans Live.
- Macros : noms libres et réglage des plages assignées au clavier.
- Macros en C++ : aujourd'hui une macro réécrit les réglages qu'elle pilote depuis l'interface, donc elle n'agit que fenêtre ouverte (une automation de `macroN` sans fenêtre ne fait rien). À faire : charger les assignations dans le processeur et moduler par-dessus les valeurs de base dans le moteur.
- Changement de type, de pente ou de placement d'une bande : fondu enchaîné au lieu d'un changement d'un coup (clic possible aujourd'hui).
