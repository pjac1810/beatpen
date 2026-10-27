# BEATPEN — boîte à rythmes façon stylet pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en boîte à rythmes de poche sur le modèle du Stylophone BEAT : 12 pads, modes DRUMS et BASS, quatre packs sonores, un looper de 2 mesures enregistré par couches, quatre patterns, un click, la transposition, le mute et un filtre sur la boucle. Projet indépendant, sans lien avec Dübreq ni M-VAVE ; tous les sons sont synthétisés (aucun échantillon).

Bâti sur [foma](https://github.com/konsumer/foma) (plateforme Felucca) : installeur web, mode secours et retour au firmware d'origine inclus.

## Sons

| Pack | Batterie (12 pads) | Basse |
|---|---|---|
| ROK | kick, snare, charleston fermé/ouvert, ride, 3 toms, crash, side-stick, cloche, double kick | basse au médiator |
| TECHNO | kick long, clap, charlestons, snare, rim, 2 toms, ride, zap, montée de bruit, blip | ligne acide résonante |
| HIPHOP | boom, snare, charlestons, clap, rim, cloche, conga, scratch avant/arrière, 2 stabs de cuivres | 808 saturée |
| BEATBOX | boom, kch, ts, tsss, pff, k, oh, ah, ee, chuh, thump, zip | voix « ou » fredonnée |

Kit et basse se combinent librement, comme sur l'original. Un étage lo-fi (réglable) donne le grain du BEAT.

## Commandes

| Commande FM-1 | Fonction |
|---|---|
| Clavier | mode DRUMS : les 12 pads, répétés le long du clavier ; mode BASS : basse chromatique avec glide |
| EDIT | DRUMS ↔ BASS |
| OCT− / OCT+ | octave de la basse (les deux : remise à zéro) |
| PRESETS / ALGORITHM | kit de batterie / son de basse : ROK, TECHNO, HIPHOP, BEATBOX |
| SELECT | tempo (40–240 BPM) |
| PLAY | lecture / arrêt de la boucle |
| REC | enregistrer par couches (à l'arrêt : démarre aussi la lecture) ; re-presser pour arrêter |
| FX, SCL, ENV, LFO | patterns 1 à 4 (en lecture : à la fin de la boucle) |
| ARP | click on/off |
| GLO | mute de la boucle : batterie, basse, les deux, rien |
| SEQ | annuler le dernier enregistrement (re-presser : rétablir) ; maintenu 1 s : effacer le pattern |
| HOME | autre page de réglages |
| KNOB 1–4 | page 1 : TRANSPOSE, FILTER (gauche passe-bas, droite passe-haut), QUANT (1/4, 1/8, 1/16), GLIDE ; page 2 : niveaux DRUMS, BASS, LO-FI, CLICK |
| SAVE | garder patterns et réglages en mémoire flash |
| MASTER | volume |
| MIDI | canal 10 : les pads (note 36 = pad 1) ; autres canaux : selon le mode |

L'écran montre la plaque métallique et ses pads (qui s'allument au jeu), la boucle sur 32 pas (batterie en orange, basse en bleu, tête de lecture), les patterns et l'état PLAY/REC.

Au premier démarrage, le pattern 1 contient un groove hip-hop : appuie sur PLAY.

## Obtenir et installer le firmware

Le compilateur JieLi du FM-1 ne tourne que sous Linux x86-64.

- **GitHub** : pousse ce dossier sur la branche `main` d'un dépôt. Le workflow compile tout ; le `.fwsc` est dans l'artefact `beatpen-fwsc` et sur la branche `firmware`. Avec Settings → Pages → Source : GitHub Actions, l'émulateur et l'installeur sont publiés sur GitHub Pages.
- **Docker Desktop** : `./fm1 build` (→ `build/beatpen-0.1.fwsc`) ou `./fm1 dev` (émulateur et installeur sur http://localhost:8000).

Installation depuis la page Install (Chrome ou Edge, FM-1 en USB avec un câble data, sans hub). **Secours** : OCT− maintenu à l'allumage ; l'onglet Rescue remet le firmware M-VAVE V15. Installer un firmware non officiel se fait à vos risques.

## Développement

- `src/sound.c` : les sons (entiers uniquement)
- `src/seq.c` : horloge, looper, patterns (dans `audio()`, précis à l'échantillon)
- `src/main.c` : interface, écran, LEDs, MIDI, sauvegarde
- `tests/run.sh` : rend chaque kit, chaque basse et une boucle enregistrée en WAV dans `build/render-out/`, vérifie la quantification et l'undo, puis fuzz
- Identité du paquet : `FM-1_94` (dans `firmware.json`)

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md).
