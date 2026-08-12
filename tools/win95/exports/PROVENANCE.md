# Provenance de la base d'exports

Ces listes ne sont pas recopiées d'une documentation. Chacune est la **table
d'exports réelle** d'une DLL extraite de la machine de test, lue par
`tools/win95/pe_symbols.py --exports`.

La distinction n'est pas rhétorique : plusieurs API que la documentation
d'époque donne pour présentes sous Windows 95 sont en réalité absentes
(`TryEnterCriticalSection`, `InterlockedCompareExchange`), et plusieurs qui sont
bien exportées ne font rien — toute la famille Unicode `...W` de KERNEL32 est un
bouchon qui renvoie 0 et pose `ERROR_CALL_NOT_IMPLEMENTED`. Voir
[`docs/research/win95-blockers.md`](../../../docs/research/win95-blockers.md).

## Système d'origine

| | |
|---|---|
| Système | Windows 95 OSR2, français |
| Installation | `~/.local/dkr-win95/vm/dkr-p2-voodoo2/win95.img`, partition C: |
| Chemin | `C:\WINDOWS\SYSTEM` |
| Machine | 86Box, Pentium II 400 MHz, 64 Mo, 3dfx |
| Extrait le | 2026-08-12 |

## Fichiers

| DLL | Exports | Taille | Date du fichier |
|---|---:|---:|---|
| `KERNEL32` | 682 | 422 400 o | 1996-08-24 |
| `MSVCRT` | 756 | 280 576 o | **1997-11-03** |
| `USER32` | 580 | 44 544 o | 1996-08-24 |
| `GDI32` | 330 | 131 072 o | 1996-08-24 |
| `ADVAPI32` | 224 | 43 008 o | 1996-08-24 |
| `WINMM` | 182 | 49 152 o | 1996-08-24 |
| `OLE32` | 162 | 558 704 o | 1996-08-24 |
| `SHELL32` | 86 | 831 488 o | 1996-08-24 |
| `WSOCK32` | 75 | 67 072 o | 1996-08-24 |
| `COMCTL32` | 64 | 379 152 o | 1996-08-24 |
| `DDRAW` | 21 | 159 744 o | 1996-08-24 |
| `COMDLG32` | 20 | 93 696 o | 1996-08-24 |
| `VERSION` | 14 | 6 656 o | 1996-08-24 |
| `DSOUND` | 5 | 86 016 o | 1996-08-24 |

Soit **3 201 symboles**.

## Deux réserves à connaître

**`MSVCRT.DLL` n'est pas d'origine.** Sa date — novembre 1997 — la distingue du
reste, daté du 24 août 1996. Elle est arrivée avec une mise à jour, pas avec le
système. **Un Windows 95 de première génération ne l'a pas du tout.** C'est la
raison pour laquelle l'ADR 0001 impose la liaison statique du CRT : en dépendre
reviendrait à faire dépendre le jeu d'une version d'Internet Explorer.

**DirectInput est absent.** Cette installation porte DirectX 2 — `DDRAW`,
`DSOUND`, `D3DIM`, `D3DRM` sont là, mais il n'y a **aucun `DINPUT.DLL`** :
DirectInput n'arrive qu'avec DirectX 3, et ne devient utilisable pour les
manettes qu'avec DirectX 5. La lecture de manette de
[E06-S02](../../../docs/stories/E06-plateforme/E06-S02-entrees-clavier-manette.md)
doit donc passer par l'API multimédia (`joyGetPosEx` de `WINMM`, présente ici),
ou le paquet doit embarquer une mise à jour de DirectX.

`DDRAW` et `DSOUND` sont versées à la base parce qu'elles existent, non parce
que le projet les vise : le rendu passe par Glide (ADR 0002) et la sortie audio
par `waveOut` de `WINMM` (E06-S03).

## Régénérer

```sh
tools/win95/check_imports.py --refresh
```

Relit les DLL depuis l'image disque de la machine de test. À refaire si le
système de référence change — et alors ce fichier doit être mis à jour avec lui.
