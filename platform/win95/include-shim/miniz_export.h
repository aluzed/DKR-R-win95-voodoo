/* E01-S05 — cale pour `miniz`, qui attend un en-tete genere par CMake.
 *
 * `miniz.h` inclut `miniz_export.h`, que le CMake de miniz fabrique a la
 * configuration pour decider des attributs d'export d'une bibliotheque
 * partagee. La cible Windows 95 ne construit pas miniz par son CMake : elle
 * compile `librecomp` directement, et tout y est statique (ADR 0001).
 *
 * Les deux macros sont donc vides, ce qui est exactement ce que le CMake de
 * miniz produit pour une bibliotheque statique. Ce fichier n'est pas un
 * contournement d'une limite de Windows 95 — c'est une piece manquante du
 * chemin de construction, au meme titre que la cale de casse de `Windows.h`.
 */
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

#endif /* MINIZ_EXPORT_H */
