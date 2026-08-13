# Provenance du relevé des exports vides

Ces listes recensent les symboles que Windows 95 **exporte sans les
implémenter**. Elles complètent `../` — qui répond à « ce symbole existe-t-il ? »
— en répondant à « et fait-il quelque chose ? ».

Livrable de [E02-S01](../../../../docs/stories/E02-systeme/E02-S01-couche-threads-synchronisation.md).

## Pourquoi cette liste existe

Un symbole **absent** est un problème bruyant : Windows 95 refuse de charger le
programme et nomme la DLL et le symbole. C'est ce que vérifie
`check_imports.py`, et c'est le garde-fou de E01-S04.

Un symbole **exporté et vide** est silencieux, et donc pire. Le lien réussit, le
chargement réussit, le contrôle des imports est satisfait — et la fonction ne
fait rien, en posant `ERROR_CALL_NOT_IMPLEMENTED` que personne ne lit.

C'est ainsi que `CreateSemaphoreW` a failli emporter tout le planificateur
d'`ultramodern` : `moodycamel::LightweightSemaphore` l'appelle, reçoit un
descripteur nul, et ni son attente ni son signal ne fonctionnent ensuite —
l'attente cesse de bloquer, le signal boucle sans fin et fige la machine.
Détail dans [`docs/research/win95-blockers.md`](../../../../docs/research/win95-blockers.md).

## Comment un bouchon est reconnu

Par sa forme, au désassemblage — pas par son nom, ni par une documentation :

```asm
33 c0              xor  eax,eax     ; valeur de retour = 0 (échec)
b1 XX              mov  cl,index    ; numéro du bouchon
e9 XX XX XX XX     jmp  queue       ; queue commune -> SetLastError(120)
```

Il n'y a pas de faux positif plausible : aucune vraie fonction ne commence par
mettre son retour à zéro pour sauter aussitôt ailleurs.

Preuve supplémentaire quand on en veut une : plusieurs bouchons **partagent la
même adresse**. `LoadLibraryExW` et `MoveFileExW` sont à la même,
`CreateEventW` et `CreateSemaphoreW` aussi. Deux fonctions au comportement
radicalement différent ne partagent du code que lorsqu'aucune des deux n'en a.

## Relevé

Mêmes DLL, même machine et même date que `../PROVENANCE.md` — Windows 95 OSR2
français, extrait le 2026-08-12.

| DLL | Bouchons | Exports nommés | Part |
|---|---:|---:|---:|
| `ADVAPI32` | 176 | 224 | **79 %** |
| `KERNEL32` | 179 | 682 | 26 % |
| `USER32` | 162 | 580 | 28 % |
| `GDI32` | 62 | 330 | 19 % |
| `MSVCRT` | 0 | 756 | 0 % |
| `WINMM` | 0 | 182 | 0 % |

Deux enseignements au-delà du cas qui a motivé le relevé :

- **`ADVAPI32` est décorative à 79 %.** Tout ticket qui la viserait — registre,
  sécurité, services — doit vérifier chaque entrée avant de s'y fier.
- **`MSVCRT` et `WINMM` n'ont aucun bouchon.** La sortie audio de
  [E06-S03](../../../../docs/stories/E06-plateforme/E06-S03-sortie-audio.md) par
  `waveOut` ne rencontrera pas ce piège.

Le motif n'est pas propre aux variantes `...W` : `BackupRead`, `CreateNamedPipeA`,
`CreateIoCompletionPort`, `GetBinaryTypeA` et `FoldStringA` en sont aussi, sans
être des API Unicode. C'est la raison pour laquelle le relevé est **mesuré et non
déduit du suffixe du nom**.

## Ce que ce relevé ne peut pas voir

Il existe une **troisième** catégorie, que ni la table d'exports ni ce relevé
n'attrapent, et il faut le savoir avant de faire confiance aux deux.

`MoveFileExA` est exportée, a du **vrai code** — même prologue que `MoveFileA`,
avec sa chaîne SEH — et n'apparaît donc pas ci-dessus. Elle échoue pourtant à
l'exécution avec `ERROR_CALL_NOT_IMPLEMENTED` : c'est une fonction qui décide de
refuser, pas une entrée vide. Mesuré par
`tools/win95/witnesses/fileio_probe.cpp` sur la machine (E02-S05).

Résumé des trois catégories, et de ce qui les révèle :

| Catégorie | Exemple | Ce qui la révèle |
|---|---|---|
| Absente de la table d'exports | `TryEnterCriticalSection` | le contrôle d'imports — bruyant, le programme ne démarre pas |
| Exportée, entrée vide | `CreateSemaphoreW` | ce relevé, par le motif au désassemblage |
| Exportée, vrai code, refuse | `MoveFileExA` | **rien d'autre que l'exécution** |

La troisième ne se déduit d'aucune analyse statique. C'est la raison pour
laquelle ce dépôt fait tourner des sondes sur la machine plutôt que de raisonner
sur des tableaux, et pourquoi un ticket qui suppose qu'une API est disponible
doit le vérifier avant de bâtir dessus.

## Régénérer

```sh
tools/win95/find_stubs.py --write /chemin/vers/KERNEL32.DLL USER32.DLL ...
```

Les DLL sont celles extraites de la machine de test par
`tools/win95/check_imports.py --refresh`. À refaire si le système de référence
change — et alors ce fichier doit être mis à jour avec lui.
