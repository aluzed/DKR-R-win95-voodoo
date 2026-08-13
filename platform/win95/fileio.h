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

/* --- Operations de systeme de fichiers ------------------------------------ *
 *
 * `std::filesystem` fournit deja tout cela, et son en-tete comme son type `path`
 * sont utilisables sous Windows 95 — mesure, voir
 * `docs/research/win95-filesystem.md`. Ce sont ses **operations** qui ne le sont
 * pas : `exists` a lui seul reclame dix-sept symboles, dont sept que le systeme
 * n'exporte pas du tout, et le binaire ne se charge alors plus.
 *
 * Les quatre ci-dessous sont exactement celles que `librecomp` emploie hors du
 * systeme de mods. Elles ne cherchent pas a reproduire `std::filesystem` : elles
 * couvrent ce qui est appele, et rien de plus.
 */

/* Rend 1 si le chemin existe, 0 sinon. Ne distingue pas « absent » de
   « inaccessible » — c'est ce que fait `std::filesystem::exists` avec un code
   d'erreur, et les appelants s'en contentent. */
int dkr_file_exists(const char *path);

/* Rend 1 si le chemin existe et est un repertoire. */
int dkr_file_is_directory(const char *path);

/* Efface un fichier. Un fichier deja absent est un succes, comme pour
   `std::filesystem::remove` : l'appelant voulait qu'il ne soit plus la. */
dkr_file_result dkr_file_remove(const char *path);

/* Cree le repertoire et tous ses parents. Un repertoire deja present est un
   succes. */
dkr_file_result dkr_file_create_directories(const char *path);

/* Copie, en ecrasant la destination si elle existe — c'est-a-dire
   `copy_options::overwrite_existing`, la seule forme employee. */
dkr_file_result dkr_file_copy(const char *from, const char *to);

/* Copie en **refusant** d'ecraser : c'est `copy_options::none`, le defaut de
   `std::filesystem::copy_file`, et deux sites d'appel en dependent — importer un
   filtre ou un pack de textures ne doit pas remplacer silencieusement celui qui
   porte deja ce nom.

   Le refus est fait par le systeme et non par un `exists` prealable : entre le
   test et la copie il y a un intervalle, si petit soit-il, et `CopyFileA` sait
   refuser toute seule. */
dkr_file_result dkr_file_copy_no_overwrite(const char *from, const char *to);

/* Rend 1 si le chemin existe et est un fichier ordinaire. */
int dkr_file_is_regular(const char *path);

/* Taille en octets. Rend 0 et pose `*ok` a 0 si le chemin n'est pas lisible —
   un fichier vide et un fichier absent rendent tous deux 0, d'ou le drapeau. */
unsigned long long dkr_file_size(const char *path, int *ok);

/* Renomme, en ecrasant la destination si elle existe. `MoveFileA` refuse
   d'ecraser et `MoveFileExA` n'est pas implementee sous Windows 95 : on efface
   donc d'abord, avec la meme fenetre de vulnerabilite que l'ecriture durable et
   pour la meme raison. */
dkr_file_result dkr_file_rename(const char *from, const char *to);

/* Rend le chemin absolu dans `out`. Sous Windows 95 c'est `GetFullPathNameA`,
   qui resout aussi les `..` — la ou `std::filesystem::absolute` ne fait que
   prefixer le repertoire courant. La difference est ecrite plutot que
   dissimulee : elle joue si un appelant compare deux chemins textuellement. */
dkr_file_result dkr_file_absolute(char *out, size_t out_size, const char *path);

/* Efface un chemin et tout ce qu'il contient. `*removed` recoit le nombre
   d'entrees reellement effacees — c'est ce que rend `std::filesystem::remove_all`,
   et un appelant peut s'en servir pour dire ce qu'il a fait.

   Il n'y a pas de version atomique : une coupure en cours d'effacement laisse
   une arborescence partielle. C'est vrai de `std::filesystem::remove_all` aussi,
   et les appelants s'en servent pour des repertoires de travail. */
dkr_file_result dkr_file_remove_all(const char *path, unsigned long long *removed);

/* Repertoire courant. Rappel de la mise en garde plus haut : sous Windows 95 un
   programme lance depuis le menu Demarrer herite d'un repertoire courant qui n'a
   rien a voir avec son emplacement. Pour trouver ou l'on est installe, c'est
   `dkr_file_app_directory` qu'il faut, jamais celle-ci. */
dkr_file_result dkr_file_current_directory(char *out, size_t out_size);

/* Repertoire des fichiers temporaires. `GetTempPathA` consulte TMP, puis TEMP,
   puis le repertoire de Windows — et rend toujours quelque chose. Sa variante
   `...W` est un bouchon, comme le reste de la famille. */
dkr_file_result dkr_file_temp_directory(char *out, size_t out_size);

/* --- Enumeration d'un repertoire ------------------------------------------ *
 *
 * `std::filesystem::directory_iterator` n'est pas reproduit : un iterateur
 * demande un cycle de vie, des categories, des comparateurs, et le code
 * appelant n'en emploie qu'une chose — parcourir une fois les entrees. On rend
 * donc la liste, ce qui se teste et se lit.
 *
 * `dkr_dir_open` rend NULL si le repertoire n'existe pas. Chaque appel a
 * `dkr_dir_next` rend le nom de l'entree suivante, sans le chemin, ou NULL a la
 * fin ; « . » et « .. » sont ecartes, comme le fait `directory_iterator`.
 */
typedef struct dkr_dir dkr_dir;

dkr_dir    *dkr_dir_open(const char *path);
const char *dkr_dir_next(dkr_dir *d);
void        dkr_dir_close(dkr_dir *d);

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
