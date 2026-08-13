/* E02-S05 — ecriture de fichiers de sauvegarde sous Windows 95.
 *
 * Le contrat, la sequence d'ecriture et surtout ce qu'elle garantit — et ce
 * qu'elle ne garantit pas — sont dans `fileio.h`. Ce fichier ne contient que la
 * mise en oeuvre.
 *
 * Comme les autres couches de `platform/win95`, il porte une implementation
 * Windows, la cible, et un vehicule POSIX qui n'existe que pour que la suite
 * tourne aussi sur l'hote. La sequence durable, elle, est commune : c'est elle
 * qu'il faut eprouver, et deux exemplaires finiraient par diverger.
 */
#include "fileio.h"

#include <string.h>

/* ========================================================================== *
 * Commun
 * ========================================================================== */

const char *dkr_file_result_text(dkr_file_result r)
{
    switch (r) {
    case DKR_FILE_OK:            return "succes";
    case DKR_FILE_ERR_PATH:      return "chemin invalide ou trop long";
    case DKR_FILE_ERR_NOT_FOUND: return "fichier introuvable";
    case DKR_FILE_ERR_ACCESS:    return "acces refuse (support protege ?)";
    case DKR_FILE_ERR_NO_SPACE:  return "disque plein";
    case DKR_FILE_ERR_NOT_READY: return "lecteur vide ou absent";
    default:                     return "erreur d'entree-sortie";
    }
}

/* Le 8.3 strict : au plus huit caracteres, un point facultatif, au plus trois.
   Aucun des caracteres que FAT refuse. On repond, on ne corrige pas. */
int dkr_file_name_is_8dot3(const char *name)
{
    const char *dot;
    size_t base_len, ext_len, i;

    if (!name || !*name) {
        return 0;
    }
    /* Les caracteres refuses par FAT, plus l'espace, que DOS accepte mal. */
    for (i = 0; name[i]; i++) {
        const char c = name[i];
        if (c == '"' || c == '*' || c == '+' || c == ',' || c == '/' ||
            c == ':' || c == ';' || c == '<' || c == '=' || c == '>' ||
            c == '?' || c == '[' || c == '\\' || c == ']' || c == '|' ||
            c == ' ') {
            return 0;
        }
    }

    dot = strchr(name, '.');
    if (dot == NULL) {
        return strlen(name) <= 8;
    }
    /* Un seul point : `SAUVE.DAT.BAK` n'est pas du 8.3. */
    if (strchr(dot + 1, '.') != NULL) {
        return 0;
    }
    base_len = (size_t)(dot - name);
    ext_len  = strlen(dot + 1);
    return base_len >= 1 && base_len <= 8 && ext_len <= 3;
}

/* Derive les noms auxiliaires du nom final. On remplace l'extension plutot que
   d'en ajouter une : `SAUVE.DAT.TMP` ne serait pas du 8.3, et le volume cible
   peut n'accepter que cela. */
static int derive_sibling(char *out, size_t out_size,
                          const char *path, const char *extension)
{
    const char *dot;
    size_t base_len;

    if (!out || !path || out_size == 0) {
        return 0;
    }
    dot = strrchr(path, '.');
    /* Un point dans un repertoire parent ne compte pas. */
    if (dot != NULL && strpbrk(dot, "\\/") != NULL) {
        dot = NULL;
    }
    base_len = dot ? (size_t)(dot - path) : strlen(path);

    if (base_len + strlen(extension) + 1 > out_size) {
        return 0;
    }
    memcpy(out, path, base_len);
    strcpy(out + base_len, extension);
    return 1;
}


#if defined(_WIN32)

/* ========================================================================== *
 * Windows — la cible
 * ========================================================================== */

#include <windows.h>
#include <stdlib.h>

static dkr_file_result from_last_error(void)
{
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:    return DKR_FILE_ERR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
    case ERROR_SHARING_VIOLATION: return DKR_FILE_ERR_ACCESS;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:  return DKR_FILE_ERR_NO_SPACE;
    case ERROR_NOT_READY:
    case ERROR_INVALID_DRIVE:     return DKR_FILE_ERR_NOT_READY;
    default:                      return DKR_FILE_ERR_IO;
    }
}

dkr_file_result dkr_file_app_directory(char *out, size_t out_size)
{
    char  path[MAX_PATH];
    DWORD n;
    char *slash;

    if (!out || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    /* `GetModuleFileNameA` et jamais le repertoire courant : lance depuis le
       menu Demarrer, un programme herite d'un courant sans rapport avec
       l'endroit ou il est installe. Mesure sur la machine : l'executable etait
       en D:\ et le courant aussi, mais c'est une coincidence du protocole de
       test, pas une propriete. */
    n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (n == 0 || n >= sizeof(path)) {
        return DKR_FILE_ERR_PATH;
    }
    slash = strrchr(path, '\\');
    if (slash == NULL) {
        return DKR_FILE_ERR_PATH;
    }
    *slash = '\0';
    if (strlen(path) + 1 > out_size) {
        return DKR_FILE_ERR_PATH;
    }
    strcpy(out, path);
    return DKR_FILE_OK;
}

static dkr_file_result write_whole(const char *path, const void *data, size_t size)
{
    HANDLE h;
    DWORD  written = 0;

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return from_last_error();
    }
    if (size > 0 && !WriteFile(h, data, (DWORD)size, &written, NULL)) {
        dkr_file_result r = from_last_error();
        CloseHandle(h);
        return r;
    }
    if (written != (DWORD)size) {
        /* Une ecriture courte sur un disque plein ne remonte pas toujours
           d'erreur : le compte rendu, lui, ne ment pas. */
        CloseHandle(h);
        return DKR_FILE_ERR_NO_SPACE;
    }
    /* Vidange avant fermeture. Sans elle, « le fichier est ecrit » ne veut rien
       dire : les donnees sont dans le cache du systeme, et c'est precisement le
       cache que la coupure de courant emporte. */
    if (!FlushFileBuffers(h)) {
        dkr_file_result r = from_last_error();
        CloseHandle(h);
        return r;
    }
    CloseHandle(h);
    return DKR_FILE_OK;
}

static int file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

dkr_file_result dkr_file_write_durable(const char *path,
                                       const void *data, size_t size)
{
    char tmp[MAX_PATH], bak[MAX_PATH];
    dkr_file_result r;

    if (!path || (!data && size > 0)) {
        return DKR_FILE_ERR_PATH;
    }
    if (!derive_sibling(tmp, sizeof(tmp), path, ".TMP") ||
        !derive_sibling(bak, sizeof(bak), path, ".BAK")) {
        return DKR_FILE_ERR_PATH;
    }

    /* 1. La nouvelle version, complete et sur le disque. */
    r = write_whole(tmp, data, size);
    if (r != DKR_FILE_OK) {
        return r;
    }

    /* 2 et 3. La precedente devient la copie de secours. `MoveFileA` refuse
       d'ecraser — mesure sur la machine — d'ou l'effacement prealable. Et
       `MoveFileExA(REPLACE_EXISTING)`, qui eviterait les deux, echoue avec
       ERROR_CALL_NOT_IMPLEMENTED sous Windows 95 : elle est exportee, elle a du
       vrai code, et elle refuse. */
    if (file_exists(path)) {
        DeleteFileA(bak);                    /* absent : sans importance */
        if (!MoveFileA(path, bak)) {
            r = from_last_error();
            DeleteFileA(tmp);
            return r;
        }
    }

    /* 4. Et la nouvelle prend sa place. C'est ici qu'est la fenetre : entre 3 et
       4, le fichier final n'existe pas. Une coupure a cet instant laisse la
       precedente dans .BAK et la nouvelle, complete, dans .TMP — et la
       relecture prefere la premiere, parce que rien ne prouve la seconde. */
    if (!MoveFileA(tmp, path)) {
        r = from_last_error();
        /* On a deja deplace l'ancienne : la remettre, sinon l'echec de la
           derniere etape aurait detruit une sauvegarde valide. */
        if (file_exists(bak) && !file_exists(path)) {
            MoveFileA(bak, path);
        }
        DeleteFileA(tmp);
        return r;
    }
    return DKR_FILE_OK;
}


/* --- Operations ------------------------------------------------------------ */

int dkr_file_exists(const char *path)
{
    return path && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

int dkr_file_is_directory(const char *path)
{
    DWORD a;
    if (!path) {
        return 0;
    }
    a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

dkr_file_result dkr_file_remove(const char *path)
{
    if (!path) {
        return DKR_FILE_ERR_PATH;
    }
    /* Le cas du repertoire vient **en premier**, et l'ordre n'est pas
       cosmetique. Ce code essayait d'abord `DeleteFileA`, puis se rabattait sur
       `RemoveDirectoryA` ; il rendait DKR_FILE_OK sur un repertoire sans rien
       effacer, parce que l'echec de `DeleteFileA` sur un repertoire passait par
       la branche « deja absent ». L'appelant croyait avoir efface, le
       repertoire restait, et le nettoyage prealable de la suite d'epreuve
       n'operait pas — c'est ainsi que le defaut s'est montre.

       Interroger le type avant d'agir coute un appel et supprime la
       possibilite de confondre « rien a faire » avec « je n'ai pas su ». */
    if (dkr_file_is_directory(path)) {
        return RemoveDirectoryA(path) ? DKR_FILE_OK : from_last_error();
    }
    if (DeleteFileA(path)) {
        return DKR_FILE_OK;
    }
    /* Deja absent : l'appelant voulait qu'il ne soit plus la, il ne l'est pas. */
    if (GetLastError() == ERROR_FILE_NOT_FOUND ||
        GetLastError() == ERROR_PATH_NOT_FOUND) {
        return DKR_FILE_OK;
    }
    return from_last_error();
}

dkr_file_result dkr_file_create_directories(const char *path)
{
    char work[MAX_PATH];
    size_t len, i;

    if (!path || !*path) {
        return DKR_FILE_ERR_PATH;
    }
    len = strlen(path);
    if (len + 1 > sizeof(work)) {
        return DKR_FILE_ERR_PATH;
    }
    memcpy(work, path, len + 1);

    /* On cree chaque niveau, du plus court au plus long. `CreateDirectoryA` ne
       cree qu'un niveau a la fois — il n'y a pas d'equivalent de
       `create_directories` sous Windows 95. */
    for (i = 0; i <= len; i++) {
        const int at_end = (i == len);
        if (!at_end && work[i] != '\\' && work[i] != '/') {
            continue;
        }
        if (i == 0) {
            continue;                       /* separateur de tete */
        }
        {
            const char saved = work[i];
            work[i] = '\0';
            /* « D: » n'est pas un repertoire a creer, c'est un volume. */
            if (!(i == 2 && work[1] == ':')) {
                if (!CreateDirectoryA(work, NULL) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    dkr_file_result r = from_last_error();
                    work[i] = saved;
                    return r;
                }
            }
            work[i] = saved;
        }
    }
    return DKR_FILE_OK;
}

dkr_file_result dkr_file_copy(const char *from, const char *to)
{
    if (!from || !to) {
        return DKR_FILE_ERR_PATH;
    }
    /* FALSE : ecraser si la destination existe, ce qui est
       `copy_options::overwrite_existing`. */
    if (!CopyFileA(from, to, FALSE)) {
        return from_last_error();
    }
    return DKR_FILE_OK;
}

dkr_file_result dkr_file_copy_no_overwrite(const char *from, const char *to)
{
    if (!from || !to) {
        return DKR_FILE_ERR_PATH;
    }
    /* TRUE : echouer si la destination existe. */
    if (!CopyFileA(from, to, TRUE)) {
        return (GetLastError() == ERROR_FILE_EXISTS)
               ? DKR_FILE_ERR_ACCESS : from_last_error();
    }
    return DKR_FILE_OK;
}


int dkr_file_is_regular(const char *path)
{
    DWORD a;
    if (!path) {
        return 0;
    }
    a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

unsigned long long dkr_file_size(const char *path, int *ok)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (ok) { *ok = 0; }
    if (!path) {
        return 0;
    }
    /* `GetFileAttributesExA` serait le choix naturel, et c'est celui que ce code
       faisait d'abord. **Windows 95 ne l'exporte pas** — pas un bouchon, une
       absence, et le chargeur refuse alors de demarrer le processus entier. Le
       controle des imports l'a arrete avant la machine ; c'est exactement le
       genre de symbole que Windows 98 a ajoute et qu'on suppose acquis.
       `GetFileSizeEx` est absent pour la meme raison.
     *
       `FindFirstFileA` rend la taille sans ouvrir le fichier — donc sans
       descripteur qui fuirait ni conflit de partage avec un fichier deja
       ouvert, ce qui etait la raison du choix initial. Elle attend un chemin
       litteral : un appelant qui y glisserait un `*` ferait mesurer une autre
       entree. Aucun ne le fait, et le nom de la fonction ne le laisse pas
       entendre. */
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    FindClose(h);
    if (ok) { *ok = 1; }
    return ((unsigned long long)fd.nFileSizeHigh << 32) |
            (unsigned long long)fd.nFileSizeLow;
}

dkr_file_result dkr_file_rename(const char *from, const char *to)
{
    if (!from || !to) {
        return DKR_FILE_ERR_PATH;
    }
    /* `MoveFileA` refuse une cible existante et `MoveFileExA` n'est pas
       implementee ici — mesure de E02-S05. On efface donc d'abord, ce qui ouvre
       la meme fenetre que l'ecriture durable : entre l'effacement et le
       renommage, ni l'ancien ni le nouveau ne sont a leur place. */
    if (dkr_file_exists(to)) {
        DeleteFileA(to);
    }
    if (!MoveFileA(from, to)) {
        return from_last_error();
    }
    return DKR_FILE_OK;
}

dkr_file_result dkr_file_absolute(char *out, size_t out_size, const char *path)
{
    DWORD n;
    if (!out || !path || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    n = GetFullPathNameA(path, (DWORD)out_size, out, NULL);
    if (n == 0 || n >= out_size) {
        return DKR_FILE_ERR_PATH;
    }
    return DKR_FILE_OK;
}

dkr_file_result dkr_file_current_directory(char *out, size_t out_size)
{
    DWORD n;
    if (!out || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    n = GetCurrentDirectoryA((DWORD)out_size, out);
    return (n == 0 || n >= out_size) ? DKR_FILE_ERR_PATH : DKR_FILE_OK;
}

struct dkr_dir {
    HANDLE           handle;
    WIN32_FIND_DATAA data;
    int              pending;   /* une entree deja lue attend d'etre rendue */
};

dkr_dir *dkr_dir_open(const char *path)
{
    char pattern[MAX_PATH];
    dkr_dir *d;
    size_t len;

    if (!path) {
        return NULL;
    }
    len = strlen(path);
    if (len + 5 > sizeof(pattern)) {
        return NULL;
    }
    memcpy(pattern, path, len);
    /* `FindFirstFileA` veut un motif, pas un repertoire. */
    if (len > 0 && pattern[len - 1] != '\\' && pattern[len - 1] != '/') {
        pattern[len++] = '\\';
    }
    strcpy(pattern + len, "*");

    d = (dkr_dir *)calloc(1, sizeof(*d));
    if (!d) {
        return NULL;
    }
    d->handle = FindFirstFileA(pattern, &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->pending = 1;
    return d;
}

const char *dkr_dir_next(dkr_dir *d)
{
    if (!d) {
        return NULL;
    }
    for (;;) {
        if (!d->pending) {
            if (!FindNextFileA(d->handle, &d->data)) {
                return NULL;
            }
        }
        d->pending = 0;
        /* « . » et « .. » sont ecartes, comme le fait `directory_iterator`. */
        if (strcmp(d->data.cFileName, ".") != 0 &&
            strcmp(d->data.cFileName, "..") != 0) {
            return d->data.cFileName;
        }
    }
}

void dkr_dir_close(dkr_dir *d)
{
    if (d) {
        if (d->handle != INVALID_HANDLE_VALUE) {
            FindClose(d->handle);
        }
        free(d);
    }
}

static dkr_file_result read_whole(const char *path, void *buffer,
                                  size_t buffer_size, size_t *read_size)
{
    HANDLE h;
    DWORD  got = 0;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return from_last_error();
    }
    if (!ReadFile(h, buffer, (DWORD)buffer_size, &got, NULL)) {
        dkr_file_result r = from_last_error();
        CloseHandle(h);
        return r;
    }
    CloseHandle(h);
    if (read_size) { *read_size = (size_t)got; }
    return DKR_FILE_OK;
}

#else

/* ========================================================================== *
 * POSIX — vehicule de test, pas une plate-forme supportee
 * ========================================================================== */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

static dkr_file_result from_errno(void)
{
    switch (errno) {
    case ENOENT:  return DKR_FILE_ERR_NOT_FOUND;
    case EACCES:
    case EROFS:
    case EPERM:   return DKR_FILE_ERR_ACCESS;
    case ENOSPC:  return DKR_FILE_ERR_NO_SPACE;
    case ENXIO:
    case ENODEV:  return DKR_FILE_ERR_NOT_READY;
    default:      return DKR_FILE_ERR_IO;
    }
}

dkr_file_result dkr_file_app_directory(char *out, size_t out_size)
{
    /* Sur l'hote, seul le comportement de la sequence durable nous interesse ;
       l'emplacement est celui du repertoire courant. */
    if (!out || out_size < 2) {
        return DKR_FILE_ERR_PATH;
    }
    strcpy(out, ".");
    return DKR_FILE_OK;
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static dkr_file_result write_whole(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return from_errno();
    }
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return DKR_FILE_ERR_NO_SPACE;
    }
    if (fflush(f) != 0) {
        fclose(f);
        return from_errno();
    }
    /* L'equivalent de FlushFileBuffers : `fflush` ne descend que jusqu'au
       systeme, `fsync` jusqu'au disque. */
    fsync(fileno(f));
    fclose(f);
    return DKR_FILE_OK;
}


/* --- Operations ------------------------------------------------------------ */

int dkr_file_exists(const char *path)
{
    return path && file_exists(path);
}

int dkr_file_is_directory(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

dkr_file_result dkr_file_remove(const char *path)
{
    if (!path) {
        return DKR_FILE_ERR_PATH;
    }
    if (unlink(path) == 0 || errno == ENOENT) {
        return DKR_FILE_OK;
    }
    if (errno == EISDIR && rmdir(path) == 0) {
        return DKR_FILE_OK;
    }
    return from_errno();
}

dkr_file_result dkr_file_create_directories(const char *path)
{
    char work[MAX_PATH];
    size_t len, i;

    if (!path || !*path) {
        return DKR_FILE_ERR_PATH;
    }
    len = strlen(path);
    if (len + 1 > sizeof(work)) {
        return DKR_FILE_ERR_PATH;
    }
    memcpy(work, path, len + 1);

    for (i = 0; i <= len; i++) {
        const int at_end = (i == len);
        if (!at_end && work[i] != '/') {
            continue;
        }
        if (i == 0) {
            continue;
        }
        {
            const char saved = work[i];
            work[i] = '\0';
            if (mkdir(work, 0777) != 0 && errno != EEXIST) {
                dkr_file_result r = from_errno();
                work[i] = saved;
                return r;
            }
            work[i] = saved;
        }
    }
    return DKR_FILE_OK;
}

static dkr_file_result copy_bytes(const char *from, const char *to,
                                  const char *mode)
{
    FILE *in, *out;
    char buf[8192];
    size_t n;

    if (!from || !to) {
        return DKR_FILE_ERR_PATH;
    }
    in = fopen(from, "rb");
    if (!in) {
        return from_errno();
    }
    out = fopen(to, mode);
    if (!out) {
        dkr_file_result r = from_errno();
        fclose(in);
        return r;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return DKR_FILE_ERR_IO;
        }
    }
    fclose(in);
    return fclose(out) == 0 ? DKR_FILE_OK : DKR_FILE_ERR_IO;
}

/* « wx » refuse une destination existante, et c'est le systeme qui tranche —
   comme le TRUE de CopyFileA sur la cible. */
dkr_file_result dkr_file_copy_no_overwrite(const char *from, const char *to)
{
    return copy_bytes(from, to, "wbx");
}

dkr_file_result dkr_file_copy(const char *from, const char *to)
{
    FILE *in, *out;
    char buf[8192];
    size_t n;

    if (!from || !to) {
        return DKR_FILE_ERR_PATH;
    }
    in = fopen(from, "rb");
    if (!in) {
        return from_errno();
    }
    out = fopen(to, "wb");
    if (!out) {
        dkr_file_result r = from_errno();
        fclose(in);
        return r;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return DKR_FILE_ERR_NO_SPACE;
        }
    }
    fclose(in);
    fclose(out);
    return DKR_FILE_OK;
}


int dkr_file_is_regular(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

unsigned long long dkr_file_size(const char *path, int *ok)
{
    struct stat st;
    if (ok) { *ok = 0; }
    if (!path || stat(path, &st) != 0) {
        return 0;
    }
    if (ok) { *ok = 1; }
    return (unsigned long long)st.st_size;
}

dkr_file_result dkr_file_rename(const char *from, const char *to)
{
    if (!from || !to) {
        return DKR_FILE_ERR_PATH;
    }
    /* On reproduit la sequence de la cible — effacer puis renommer — plutot que
       d'employer le `rename` de POSIX, qui ecrase de lui-meme. Un vehicule de
       test qui prend un raccourci que la cible n'a pas ne teste pas la cible. */
    if (dkr_file_exists(to)) {
        unlink(to);
    }
    if (rename(from, to) != 0) {
        return from_errno();
    }
    return DKR_FILE_OK;
}

/* Resout « . » et « .. » sur place, sans toucher au disque.
 *
 * `GetFullPathNameA` le fait pour la cible ; sans equivalent ici, la dorsale de
 * test se comporterait autrement que la plate-forme qu'elle sert a eprouver, et
 * l'epreuve ne prouverait rien. C'est le nouveau montage — la branche Windows 95
 * compilee sur l'hote — qui l'a montre : `weakly_canonical` y rendait deux
 * chemins differents pour un meme fichier.
 *
 * La resolution est purement lexicale, comme celle de Windows : elle ne suit pas
 * les liens symboliques et n'exige pas que le chemin existe. C'est exactement ce
 * que demande `weakly_canonical`, et `realpath` en ferait trop. */
static void normalize_lexically(char *p)
{
    char *out = p;
    char *seg = p;
    int   absolute = (p[0] == '/');

    if (absolute) { out++; seg++; }
    while (*seg) {
        char *end = strchr(seg, '/');
        size_t len = end ? (size_t)(end - seg) : strlen(seg);

        if (len == 0 || (len == 1 && seg[0] == '.')) {
            /* rien : un separateur double ou « . » ne dit rien */
        } else if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            /* Remonter : effacer le dernier segment ecrit. A la racine, « .. »
               ne mene nulle part et s'ignore — comme sous Windows. */
            char *base = p + (absolute ? 1 : 0);
            if (out > base) {
                out--;                                   /* le '/' de fin */
                while (out > base && out[-1] != '/') { out--; }
            }
        } else {
            memmove(out, seg, len);
            out += len;
            *out++ = '/';
        }
        if (!end) { break; }
        seg = end + 1;
    }
    /* Retirer le separateur final, sauf s'il est la racine a lui seul. */
    if (out > p + (absolute ? 1 : 0) && out[-1] == '/') { out--; }
    if (out == p) { *out++ = absolute ? '/' : '.'; }
    *out = '\0';
}

dkr_file_result dkr_file_absolute(char *out, size_t out_size, const char *path)
{
    char resolved[MAX_PATH];
    dkr_file_result r;

    if (!out || !path || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    if (path[0] == '/') {
        if (strlen(path) + 1 > out_size) {
            return DKR_FILE_ERR_PATH;
        }
        strcpy(out, path);
        normalize_lexically(out);
        return DKR_FILE_OK;
    }
    if (!getcwd(resolved, sizeof(resolved))) {
        return DKR_FILE_ERR_PATH;
    }
    r = dkr_file_join(out, out_size, resolved, path);
    if (r == DKR_FILE_OK) {
        normalize_lexically(out);
    }
    return r;
}

struct dkr_dir {
    DIR *handle;
};

dkr_dir *dkr_dir_open(const char *path)
{
    dkr_dir *d;
    if (!path) {
        return NULL;
    }
    d = (dkr_dir *)calloc(1, sizeof(*d));
    if (!d) {
        return NULL;
    }
    d->handle = opendir(path);
    if (!d->handle) {
        free(d);
        return NULL;
    }
    return d;
}

const char *dkr_dir_next(dkr_dir *d)
{
    struct dirent *e;
    if (!d) {
        return NULL;
    }
    while ((e = readdir(d->handle)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            return e->d_name;
        }
    }
    return NULL;
}

void dkr_dir_close(dkr_dir *d)
{
    if (d) {
        if (d->handle) { closedir(d->handle); }
        free(d);
    }
}

static dkr_file_result read_whole(const char *path, void *buffer,
                                  size_t buffer_size, size_t *read_size)
{
    FILE  *f = fopen(path, "rb");
    size_t got;
    if (!f) {
        return from_errno();
    }
    got = fread(buffer, 1, buffer_size, f);
    fclose(f);
    if (read_size) { *read_size = got; }
    return DKR_FILE_OK;
}

/* `rename` de POSIX ecrase, la ou `MoveFileA` refuse. On reproduit malgre tout
   la sequence de la cible — effacer puis renommer — parce que c'est **elle**
   que la suite doit eprouver. Un vehicule de test qui prendrait un raccourci
   que la cible n'a pas ne testerait pas la cible. */
static int move_no_replace(const char *from, const char *to)
{
    if (file_exists(to)) {
        errno = EEXIST;
        return 0;
    }
    return rename(from, to) == 0;
}

dkr_file_result dkr_file_write_durable(const char *path,
                                       const void *data, size_t size)
{
    char tmp[MAX_PATH], bak[MAX_PATH];
    dkr_file_result r;

    if (!path || (!data && size > 0)) {
        return DKR_FILE_ERR_PATH;
    }
    if (!derive_sibling(tmp, sizeof(tmp), path, ".TMP") ||
        !derive_sibling(bak, sizeof(bak), path, ".BAK")) {
        return DKR_FILE_ERR_PATH;
    }

    r = write_whole(tmp, data, size);
    if (r != DKR_FILE_OK) {
        return r;
    }
    if (file_exists(path)) {
        unlink(bak);
        if (!move_no_replace(path, bak)) {
            r = from_errno();
            unlink(tmp);
            return r;
        }
    }
    if (!move_no_replace(tmp, path)) {
        r = from_errno();
        if (file_exists(bak) && !file_exists(path)) {
            move_no_replace(bak, path);
        }
        unlink(tmp);
        return r;
    }
    return DKR_FILE_OK;
}

dkr_file_result dkr_file_current_directory(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    return getcwd(out, out_size) ? DKR_FILE_OK : DKR_FILE_ERR_PATH;
}

#endif /* _WIN32 */


/* ========================================================================== *
 * Commun : effacement recursif
 * ========================================================================== *
 *
 * Ecrit une seule fois, au-dessus des primitives, et non deux fois dans chaque
 * dorsale : c'est de la logique d'arborescence, pas d'appel systeme. Les deux
 * cibles executent donc exactement le meme parcours, ce qui est precisement ce
 * qu'on cherche a garantir ailleurs a coups d'epreuves.
 *
 * La recursion est bornee par la profondeur de l'arborescence, et MAX_PATH la
 * borne a son tour : un chemin qui ne tient pas dans le tampon fait echouer la
 * jointure avant l'appel recursif.
 *
 * Le repertoire est ferme **avant** de descendre dans ses entrees. Garder une
 * recherche ouverte sur un repertoire dont on efface le contenu est le genre de
 * chose que Windows 95 tolere mal, et rien n'oblige a le faire : la liste des
 * noms est recopiee d'abord.
 */
dkr_file_result dkr_file_remove_all(const char *path, unsigned long long *removed)
{
    /* Les noms d'un niveau, recopies avant de descendre. La borne est celle
       d'un repertoire de travail ordinaire ; au-dela, on traite ce qu'on a vu
       puis on recommence, plutot que de renoncer ou de grossir sans fin. */
    enum { BATCH = 64 };
    char names[BATCH][MAX_PATH];
    char child[MAX_PATH];
    unsigned long long n = 0;
    int again = 1;

    if (removed) { *removed = 0; }
    if (!path) {
        return DKR_FILE_ERR_PATH;
    }
    if (!dkr_file_exists(path)) {
        return DKR_FILE_OK;      /* rien a faire : comme std::filesystem */
    }

    while (again && dkr_file_is_directory(path)) {
        dkr_dir *d;
        const char *name;
        int count = 0, i;

        again = 0;
        d = dkr_dir_open(path);
        if (!d) {
            break;
        }
        while (count < BATCH && (name = dkr_dir_next(d)) != NULL) {
            size_t len = strlen(name);
            if (len >= sizeof(names[0])) {
                continue;        /* impossible a former : signale plus bas */
            }
            memcpy(names[count], name, len + 1);
            count++;
        }
        /* S'il restait des entrees, on repassera. */
        again = (count == BATCH) && (dkr_dir_next(d) != NULL);
        dkr_dir_close(d);

        for (i = 0; i < count; i++) {
            unsigned long long sub = 0;
            if (dkr_file_join(child, sizeof(child), path, names[i])
                != DKR_FILE_OK) {
                return DKR_FILE_ERR_PATH;
            }
            if (dkr_file_remove_all(child, &sub) != DKR_FILE_OK) {
                if (removed) { *removed = n + sub; }
                return DKR_FILE_ERR_IO;
            }
            n += sub;
        }
        if (count == 0) {
            break;
        }
    }

    if (dkr_file_remove(path) != DKR_FILE_OK) {
        if (removed) { *removed = n; }
        return DKR_FILE_ERR_IO;
    }
    n++;
    if (removed) { *removed = n; }
    return DKR_FILE_OK;
}


/* ========================================================================== *
 * Commun : la relecture, et son ordre de preference
 * ========================================================================== */

dkr_file_result dkr_file_join(char *out, size_t out_size,
                              const char *directory, const char *name)
{
    size_t d_len, n_len;
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif

    if (!out || !directory || !name || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    d_len = strlen(directory);
    n_len = strlen(name);
    /* Un separateur de trop est un chemin invalide sous DOS. */
    while (d_len > 0 && (directory[d_len - 1] == '\\' || directory[d_len - 1] == '/')) {
        d_len--;
    }
    if (d_len + 1 + n_len + 1 > out_size) {
        return DKR_FILE_ERR_PATH;
    }
    memcpy(out, directory, d_len);
    out[d_len] = sep;
    strcpy(out + d_len + 1, name);
    return DKR_FILE_OK;
}

dkr_file_result dkr_file_read_durable(const char *path,
                                      void *buffer, size_t buffer_size,
                                      size_t *read_size, int *from_backup)
{
    char bak[260];
    dkr_file_result r;

    if (from_backup) { *from_backup = 0; }
    if (!path || !buffer) {
        return DKR_FILE_ERR_PATH;
    }

    r = read_whole(path, buffer, buffer_size, read_size);
    if (r == DKR_FILE_OK) {
        return r;
    }
    /* Un echec autre que « absent » ne doit pas faire basculer sur la copie de
       secours : un support protege ou un fichier verrouille se dit tel quel,
       sinon le joueur croirait avoir perdu sa derniere partie alors que le
       fichier est intact. */
    if (r != DKR_FILE_ERR_NOT_FOUND) {
        return r;
    }

    if (!derive_sibling(bak, sizeof(bak), path, ".BAK")) {
        return DKR_FILE_ERR_PATH;
    }
    r = read_whole(bak, buffer, buffer_size, read_size);
    if (r == DKR_FILE_OK && from_backup) {
        *from_backup = 1;
    }
    /* `.TMP` n'est jamais relu, meme s'il est la : rien ne prouve qu'il soit
       complet, et le format de DKR-R ne porte pas de somme de controle qui
       permettrait de le verifier sans le modifier. Perdre la derniere ecriture
       est desagreable ; charger une sauvegarde tronquee ne l'est pas moins et
       se decouvre bien plus tard. */
    return r;
}
