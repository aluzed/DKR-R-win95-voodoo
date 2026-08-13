/* E02-S05 — ecriture de fichiers de sauvegarde sous Windows 95.
 *
 * « Perdre la progression d'un joueur est le defaut le moins pardonnable d'un
 * portage. » Sur une machine de 1998 sans onduleur, l'extinction pendant une
 * ecriture n'est pas un cas limite : c'est un scenario courant. Tout ce fichier
 * decoule de la.
 *
 * ## Ce que la machine permet, mesure et non suppose
 *
 * Releve par `tools/win95/witnesses/fileio_probe.cpp` ; detail dans
 * `docs/research/win95-fileio.md`.
 *
 *   MoveFileExA(REPLACE_EXISTING)   ECHOUE — erreur 120, non implementee
 *   MoveFileA sur cible existante   refuse, comme documente
 *   noms longs sur le volume FAT16  fonctionnent (VFAT), alias 8.3 lisible
 *   GetModuleFileNameA              donne le repertoire de l'executable
 *   ecriture sur support absent     refusee proprement, erreur exploitable
 *
 * Le premier point commande tout : **il n'existe pas de remplacement atomique**.
 * Le ticket l'annoncait et la mesure le confirme — a la difference de deux
 * autres de ses suppositions, dementies par E02-S03. La sequence doit donc etre
 * ecrite a la main, et sa fenetre de vulnerabilite assumee.
 *
 * ## La sequence, et ce qu'elle garantit
 *
 *   1. ecrire      SAUVE.TMP, vider les tampons, fermer
 *   2. effacer     SAUVE.BAK
 *   3. renommer    SAUVE.DAT -> SAUVE.BAK
 *   4. renommer    SAUVE.TMP -> SAUVE.DAT
 *
 * Entre 3 et 4, `SAUVE.DAT` n'existe pas. Une coupure a cet instant laisse la
 * sauvegarde precedente dans `SAUVE.BAK` et la nouvelle, complete, dans
 * `SAUVE.TMP`.
 *
 * **La garantie offerte n'est donc pas « on ne perd jamais la derniere
 * ecriture », mais « on ne perd jamais une sauvegarde valide ».** C'est la
 * distinction qui compte : perdre la derniere course est desagreable, perdre la
 * progression entiere ne se pardonne pas.
 *
 * A la relecture, l'ordre de preference est donc :
 *
 *   SAUVE.DAT   s'il existe — l'ecriture est allee au bout
 *   SAUVE.BAK   sinon — la precedente, connue bonne
 *   SAUVE.TMP   jamais : rien ne prouve qu'elle soit complete, et le format de
 *               DKR-R ne porte pas de somme de controle qui permettrait de le
 *               verifier sans le modifier
 */
#ifndef DKR_WIN95_FILEIO_H
#define DKR_WIN95_FILEIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Codes de retour. Distingues parce que l'appelant doit pouvoir dire au joueur
   *pourquoi* sa sauvegarde a echoue — « disque plein » et « support protege »
   n'appellent pas le meme geste. */
typedef enum {
    DKR_FILE_OK = 0,
    DKR_FILE_ERR_PATH,        /* chemin invalide ou trop long */
    DKR_FILE_ERR_NOT_FOUND,   /* rien a lire */
    DKR_FILE_ERR_ACCESS,      /* support en lecture seule, fichier verrouille */
    DKR_FILE_ERR_NO_SPACE,    /* disque plein */
    DKR_FILE_ERR_NOT_READY,   /* lecteur vide ou absent */
    DKR_FILE_ERR_IO           /* le reste */
} dkr_file_result;

const char *dkr_file_result_text(dkr_file_result r);

/* --- Emplacement ---------------------------------------------------------- *
 *
 * Il n'y a pas de `%APPDATA%` sous Windows 95, et le dossier de l'application
 * est l'usage de l'epoque. Un programme lance depuis le menu Demarrer herite
 * d'un repertoire courant qui n'a rien a voir avec l'endroit ou il est
 * installe : c'est `GetModuleFileNameA` qui fait foi, jamais le repertoire
 * courant.
 */
dkr_file_result dkr_file_app_directory(char *out, size_t out_size);

/* Assemble `directory` et `name` avec le separateur qui convient. */
dkr_file_result dkr_file_join(char *out, size_t out_size,
                              const char *directory, const char *name);

/* --- Ecriture durable ------------------------------------------------------ *
 *
 * Applique la sequence decrite en tete de fichier. `path` est le nom final ;
 * les fichiers `.TMP` et `.BAK` sont derives de lui.
 *
 * `flush` vide les tampons avant de fermer. Le laisser a zero rend l'ecriture
 * plus rapide et sans garantie — ce qui n'a de sens que pour un fichier dont la
 * perte est sans consequence.
 */
dkr_file_result dkr_file_write_durable(const char *path,
                                       const void *data, size_t size);

/* Lit le fichier en appliquant l'ordre de preference decrit plus haut.
   `*read_size` recoit la taille lue. Si `from_backup` est non nul, il recoit 1
   quand la copie de secours a servi — l'appelant peut alors le dire au joueur
   plutot que de le laisser decouvrir qu'il a perdu sa derniere course. */
dkr_file_result dkr_file_read_durable(const char *path,
                                      void *buffer, size_t buffer_size,
                                      size_t *read_size, int *from_backup);

/* --- Noms de fichiers ------------------------------------------------------ *
 *
 * Le volume de test est en FAT16 avec les noms longs actifs, et un nom long y
 * survit a l'aller-retour. Mais rien ne garantit que ce soit vrai partout : un
 * Windows 95 de premiere generation sans VFAT, ou un volume monte autrement,
 * ramene au 8.3.
 *
 * Cette fonction ne corrige rien — elle **repond**, pour que l'appelant decide
 * en connaissance de cause plutot que de decouvrir la troncature apres coup.
 * Rend 1 si `name` tient dans le 8.3 strict. */
int dkr_file_name_is_8dot3(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_FILEIO_H */
