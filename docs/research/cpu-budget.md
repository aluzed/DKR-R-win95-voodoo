# Coût CPU du code recompilé en 32 bits sans SSE

Mesures de [E00-S03](../stories/E00-cadrage/E00-S03-spike-budget-cpu-recompilation.md).
Date : 2026-08-11.

## Résumé

| Grandeur | Mesure |
|---|---|
| Expansion x86 par instruction MIPS, 64 bits | **2,74** |
| Expansion x86 par instruction MIPS, 32 bits sans SSE | **3,89** |
| Surcoût en nombre d'instructions | **1,42×** |
| Surcoût en taille de code (`.text`) | **1,25×** |
| Surcoût en temps d'exécution, médian | **2,16×** |
| Surcoût en temps d'exécution, étendue | 1,54× à 3,00× |
| Normalisation hôte 32 bits → Pentium II 400 MHz | **17,7×** (étendue 15,6× à 21,7×) |
| Facteur global, cœur moderne 64 bits → cible | **≈ 38×** |

**Le code recompilé compile et s'exécute en 32 bits sans SSE.** Un seul blocage a
été rencontré, il est corrigé, et il n'était pas dans le code du jeu.

Le facteur qui manquait — la normalisation vers la machine cible — **est
désormais mesuré**, sur le Pentium II émulé, avec le même code et les mêmes
entrées que sur l'hôte. Le go/no-go reste cependant non prononcé : il lui manque
maintenant un dénominateur, le coût CPU réel d'une image de jeu. Voir
« Ce qui manque pour conclure ».

## Ce qui a été construit pour mesurer

La mesure exigeait du vrai code généré, qui n'existait pas : `RecompiledFuncs`
est absent du dépôt et sa production passait par Windows.

| Étape | Résultat |
|---|---|
| Toolchain MIPS, cmake, ninja, sans droits root | binutils extraits de paquets `.deb` dans un préfixe utilisateur |
| ELF du decomp de référence | `dkr.us.v77.elf`, 10,9 Mo, 3 994 symboles |
| ROM produite par ce build | SHA-1 `0cb115d8…b9670`, **identique à la ROM du joueur** — build matching |
| Configuration N64Recomp | `scripts/generate_recomp_toml.py`, portage Linux de l'étape TOML du script PowerShell |
| Code généré | **37 fichiers, 587 625 lignes de C, 20 Mo, 1 810 fonctions émises** |

Le decomp voisin donnait sa toolchain MIPS pour absente ; elle ne l'est plus, et
l'oracle du projet est constructible sous Linux.

## Le blocage rencontré, et pourquoi il ne condamne rien

En 32 bits, la compilation s'arrête avant la première fonction :

```
recomp.h:70:2: error: #error "128-bit integer type not found"
```

`recomp.h` implémente les instructions MIPS `DMULT`/`DMULTU` avec `__int128`,
avec repli sur les intrinsèques MSVC `_mul128`. Aucun des deux n'existe en
32 bits. Comme Windows 95 est 32 bits par définition, cela bloquait toute la
cible.

Deux faits mesurés ont changé la portée du problème :

- ce type n'est utilisé **que** par `DMULT` et `DMULTU`, nulle part ailleurs ;
- **DKR ne les appelle jamais** : zéro occurrence de l'un ou l'autre sur les
  3 823 fonctions recompilées.

Un bouchon aurait donc suffi. C'est précisément pourquoi il n'en a pas été mis
un : la fonction serait restée fausse pour tout portage ultérieur dont le jeu, lui,
les utilise. Le patch
`patches/n64recomp/0002-portable-128-bit-multiply-for-32-bit-targets.patch`
implémente la multiplication longue à partir de quatre produits partiels de
32 bits, avec correction du signe sur le mot haut.

Vérifiée contre l'implémentation `__int128` sur x86-64 : 10 000 000 de paires
aléatoires plus le produit cartésien complet des valeurs frontières de signe et
de débordement, soit **20 000 200 comparaisons, zéro écart**.

Une fois ce patch appliqué, les **37 fichiers sur 37** compilent dans les deux
configurations.

## Absence de SSE, vérifiée

Le désassemblage complet des objets 32 bits ne contient **aucune** instruction
SSE — ni `movss`/`movsd`, ni `cvtsi2sd`, ni `pxor`/`movaps`. Les options
`-march=pentium2 -mfpmath=387 -mno-sse` tiennent sur l'ensemble du code généré.

Le profil des instructions émises correspond à ce qu'on attend d'une émulation
de registres 64 bits sur une machine 32 bits :

| Instruction | Occurrences | Ce qu'elle traduit |
|---|---|---|
| `mov` | 499 015 | déplacement des paires de mots formant un registre du VR4300 |
| `sar` | 49 078 | extension de signe, omniprésente dans le jeu d'instructions MIPS |
| `fstp`, `fucomi`, `jp` | ~43 700 | arithmétique flottante sur la pile x87 |

## Temps d'exécution

### Méthode

Un banc d'essai charge les segments `PT_LOAD` de l'ELF matching dans une image
mémoire de 512 Mio couvrant toute la fenêtre KSEG0, puis appelle des fonctions
**feuilles** réelles du jeu — celles qui n'appellent aucune autre fonction, donc
mesurables isolément. Sur 722 feuilles disponibles, les 48 plus grosses ont été
retenues, et 24 s'exécutent sans fauter contre une image mémoire statique.

Les deux binaires sont construits depuis les mêmes sources, avec les mêmes
options hormis `-m32 -march=pentium2 -mfpmath=387 -mno-sse`. Trois campagnes de
3 000 000 d'appels par fonction ; le minimum des trois est retenu.

Un premier jeu de mesures a dû être jeté : le point de reprise `sigsetjmp` était
posé à l'intérieur de la boucle chronométrée, et sa sauvegarde de masque de
signaux fait un appel système. Les temps étaient alors uniformes à ~226 ns, soit
le coût du harnais et non celui du code mesuré.

### Résultats

| Fonction | 64 bits | 32 bits | Facteur |
|---|---:|---:|---:|
| `search_level_properties_forwards` | 5,40 ns | 16,20 ns | 3,00× |
| `slowly_change_fog` | 23,90 ns | 59,80 ns | 2,50× |
| `particle_allocate` | 3,80 ns | 9,40 ns | 2,47× |
| `apply_vehicle_rotation_offset` | 4,30 ns | 10,40 ns | 2,42× |
| `__sinf_recomp` | 4,70 ns | 11,00 ns | 2,34× |
| `debug_text_character` | 6,50 ns | 14,10 ns | 2,17× |
| `get_wave_properties` | 5,80 ns | 11,40 ns | 1,97× |
| `void_generate_primitive` | 6,10 ns | 10,80 ns | 1,77× |
| `light_update_ambience` | 4,40 ns | 7,00 ns | 1,59× |
| `resolve_collisions` | 5,00 ns | 7,70 ns | 1,54× |
| **Somme des 24 fonctions** | **140,70 ns** | **304,80 ns** | **2,17×** |

Facteur médian **2,16×**, étendue 1,54× à 3,00×.

Le surcoût en temps (2,16×) dépasse le surcoût en nombre d'instructions (1,42×).
L'écart est attendu : les instructions ajoutées ne sont pas gratuites, et le code
plus volumineux de 25 % pèse davantage sur le cache d'instructions — effet qui
sera **plus marqué**, pas moins, sur un Pentium II dont les caches se comptent en
dizaines de kilooctets.

## Normalisation vers la machine cible

Le banc a été porté sur Win32 (`tools/cpu-budget/bench_win32.c`) et exécuté sur la
machine de test — Pentium II 400 MHz, 64 Mo, Windows 95 OSR2.5 — avec le **même
code généré, le même ELF, les mêmes fonctions et les mêmes entrées** que sur
l'hôte. Seule la machine change.

23 des 48 fonctions candidates s'exécutent sans fauter sur la cible, contre 24 sur
l'hôte : la fenêtre RDRAM y est de 16 Mio au lieu de 512, parce que committer
512 Mio sur une machine de 64 Mo ferait paginer — et une mesure de temps sous
pagination ne vaut rien. La comparaison ci-dessous porte sur les 23 fonctions
communes.

| Fonction | Hôte 32 bits | Cible | Facteur |
|---|---:|---:|---:|
| `func_80072E28` | 4,50 ns | 97,6 ns | 21,7× |
| `search_level_properties_backwards` | 11,80 ns | 237,5 ns | 20,1× |
| `debug_text_character` | 14,10 ns | 267,8 ns | 19,0× |
| `func_8002F2AC` | 24,90 ns | 451,5 ns | 18,1× |
| `resolve_collisions` | 7,70 ns | 134,0 ns | 17,4× |
| `__sinf_recomp` | 11,00 ns | 187,7 ns | 17,1× |
| `func_8001E4C4` | 10,80 ns | 168,6 ns | 15,6× |
| **Somme des 23 fonctions** | **245,0 ns** | **4 353,3 ns** | **17,8×** |

Facteur médian **17,7×**, étendue **15,6× à 21,7×**. La dispersion est étroite —
un facteur trois fois plus resserré que celui du passage 64 → 32 bits — ce qui
indique une mesure saine plutôt qu'un artefact.

**Mise en perspective.** L'hôte tourne à ~3,5 GHz, la cible à 400 MHz : un rapport
de fréquence d'environ 9. Le facteur mesuré étant de 17,7, le Pentium II est
environ **deux fois moins efficace par cycle** que le cœur moderne sur ce code.
C'est plausible pour du code recompilé, fait de longues chaînes de dépendances
que l'exécution dans le désordre ne peut pas beaucoup recouvrir, et il est
rassurant que le chiffre tombe dans l'ordre de grandeur attendu plutôt que dans
un extrême.

Combiné au facteur 64 → 32 bits, le rapport entre le poste de développement et la
machine cible est d'environ **38×**. C'est le coefficient à appliquer à toute
mesure faite sur l'hôte pour l'estimer sur la cible.

### Ce que cette mesure ne dit pas

**La cible est un Pentium II émulé, pas un vrai.** 86Box modélise les temps
d'instruction du processeur, et la mesure est prise dans le temps *émulé*
(compteur à 1 193 180 Hz vu par l'invité), pas en temps réel de l'hôte. Elle
reflète donc ce que le modèle de 86Box prédit d'un Pentium II 400 — un modèle
raisonnable, mais qui ne reproduit ni les caches réels, ni la prédiction de
branchement, ni la bande passante mémoire d'époque. La validation sur matériel
réel ([E09-S04](../stories/E09-qa/E09-S04-validation-materiel-reel.md)) reste
indispensable, et c'est elle qui dira de combien ce modèle se trompe.

## Limites de ces chiffres

À énoncer avant toute conclusion :

1. **Ce sont des fonctions feuilles, appelées contre une image mémoire statique.**
   Elles empruntent vraisemblablement des chemins de sortie précoce faute d'état
   de jeu reconstruit. Le mélange d'instructions est réel, la profondeur
   d'exécution ne l'est pas.
2. **La mesure est faite sur un Ryzen 9 3950X, pas sur un Pentium II.** Elle
   établit le coût du passage 64 → 32 bits sans SSE, ce qui est exactement ce que
   le ticket demandait de mesurer — mais pas le rapport à la machine cible.
3. **24 fonctions sur 48**, choisies parce qu'elles ne fautent pas. Le
   sous-ensemble est identique dans les deux configurations, ce que la
   comparaison exige, mais il n'est pas un échantillon représentatif d'une image
   de jeu.

## Ce qui manque pour conclure

Les deux facteurs sont désormais mesurés. Ce qui manque n'est plus une
normalisation mais un **dénominateur** : combien de travail CPU DKR demande
réellement par image.

Le banc mesure des fonctions feuilles appelées isolément. Il donne le coût
*relatif* d'une machine à l'autre, ce qui était l'objet du ticket, mais pas le
coût *absolu* d'une image de jeu. Pour prononcer le go/no-go il faut :

1. **Faire tourner le jeu sous Windows 95** ([E02-S06](../stories/E02-systeme/E02-S06-amorcage-jeu.md)),
   avec le renderer de diagnostic, et mesurer le temps CPU par image. C'est le
   chiffre qui manque, et lui seul se compare aux 33,3 ms.
2. **Chiffrer le microcode audio** ([E00-S04](../stories/E00-cadrage/E00-S04-spike-cout-rsp-sans-sse.md)).
   C'est le poste le plus inquiétant du budget : la console le confiait à un DSP
   vectoriel dédié à 62,5 MHz, il tombe ici sur le même processeur. La génération
   de son code est possible — `RSPRecomp` lit la ROM directement.
3. **Confronter au matériel réel** ([E09-S04](../stories/E09-qa/E09-S04-validation-materiel-reel.md)),
   pour mesurer l'écart entre le modèle de 86Box et un vrai Pentium II.

En attendant, le facteur **38×** permet de transposer sur la cible toute mesure
faite sur le poste de développement, ce qui est exploitable dès maintenant par
E08-S01.

## Ce qu'on peut déjà dire

Aucun élément mesuré ne condamne l'approche, et deux la soutiennent :

- le seul blocage de compilation était dans un en-tête, il est levé, et le jeu ne
  touchait pas le code fautif ;
- 3,89 instructions x86 par instruction MIPS reste un facteur d'expansion
  ordinaire pour de la recompilation statique.

Le poste qui reste entièrement non mesuré, et qui est le plus inquiétant, n'est
pas le CPU du jeu : c'est le microcode audio `aspMain`, exécuté ici sur le
processeur hôte alors que la console le confiait à un DSP vectoriel dédié à
62,5 MHz. Il fait l'objet de [E00-S04](../stories/E00-cadrage/E00-S04-spike-cout-rsp-sans-sse.md),
et la génération de son code est désormais possible — `RSPRecomp` lit la ROM
directement, sans passer par l'ELF.

## Reproduire

```bash
# 1. outils, sans droits root
scripts/Setup-Win95-Toolchain.sh

# 2. ELF de reference, dans le decomp voisin
export PATH="$HOME/.local/dkr-win95/bin:$PATH"
cd ../Diddy-Kong-Racing
uv venv .venv --python 3.12 && uv pip install --python .venv/bin/python -r requirements.txt
.venv/bin/python ver/splat/update_baserom_names.py && make -C tools
make extract && make -j"$(nproc)"

# 3. sources recompilees
cd ../DKR-R-win95-voodoo
scripts/generate_recomp_toml.py \
  --elf ../Diddy-Kong-Racing/build/dkr.us.v77.elf \
  --rom ../Diddy-Kong-Racing/build/dkr.us.v77.z64 \
  --policy runtime-recomp/dkr.us.v77.recomp-policy.json \
  --output-funcs runtime-recomp/RecompiledFuncs \
  --output runtime-recomp/dkr.us.v77.generated.toml
extern/n64-modern-runtime/N64Recomp/build-linux/N64Recomp \
  runtime-recomp/dkr.us.v77.generated.toml

# 4. mesure
tools/cpu-budget/run.sh ../Diddy-Kong-Racing/build/dkr.us.v77.elf
```

Le decomp ne crée pas son environnement Python avec `python3 -m venv` sur ce
poste : cela exige le paquet `python3-venv`, donc `apt`, donc les droits root.
`uv` crée le même environnement sans `ensurepip`.

`tools/cpu-budget/run.sh` refait toute la mesure de bout en bout : preuve
d'équivalence de la multiplication 128 bits, sélection des fonctions feuilles,
double compilation, vérification d'absence de SSE, et trois campagnes de mesure.
