/* E02-S05 — point d'indirection des operations de systeme de fichiers.
 *
 * `std::filesystem::path` **fonctionne** sous Windows 95 : l'en-tete ne coute
 * rien, le type ne tire qu'un bouchon, et sa manipulation a ete verifiee sur la
 * machine — construction, `parent_path`, `filename`, `extension`,
 * concatenation. C'est de la manipulation de chaines, et cela ne demande rien au
 * systeme. Voir `docs/research/win95-filesystem.md`.
 *
 * Ce sont ses **operations** qui ne passent pas : `exists` a lui seul reclame
 * dix-sept symboles, dont sept que Windows 95 n'exporte pas du tout — et le
 * binaire cesse alors de se charger.
 *
 * La consequence sur la forme de ce fichier est importante, et c'est ce qui le
 * distingue d'une couche d'abstraction ordinaire : **le type ne change pas**.
 * Les signatures gardent `std::filesystem::path`, les 250 usages du type restent
 * intacts, et seuls les appels d'operations sont detournes. Un portage qui
 * aurait remplace le type aurait touche cinq fois plus de code pour un gain nul,
 * et fait diverger la cible moderne de l'oracle.
 */
#ifndef DKR_WIN95_FILEIO_HPP
#define DKR_WIN95_FILEIO_HPP

#include <cstdint>
#include <filesystem>
#include <system_error>
#include <vector>

namespace dkr::fs {

#if defined(DKR_TARGET_WIN95)

} // namespace dkr::fs

#include "fileio.h"

namespace dkr::fs {

/* Les chemins sont convertis en `string()` — c'est-a-dire en octets etroits —
   et non en `wstring()`. Sous Windows 9x la famille `...W` est un bouchon
   (E01-S03), et la couche de dessous n'appelle donc que les `...A`. */
inline bool exists(const std::filesystem::path &p)
{
    return dkr_file_exists(p.string().c_str()) != 0;
}

inline bool exists(const std::filesystem::path &p, std::error_code &ec)
{
    ec.clear();
    /* Qualifie : l'argument etant un `std::filesystem::path`, la recherche par
       argument trouverait `std::filesystem::exists` et l'appel serait ambigu. */
    return dkr::fs::exists(p);
}

inline bool is_directory(const std::filesystem::path &p)
{
    return dkr_file_is_directory(p.string().c_str()) != 0;
}

inline bool is_directory(const std::filesystem::path &p, std::error_code &ec)
{
    ec.clear();
    return dkr::fs::is_directory(p);
}

/* `std::filesystem::remove` rend true si quelque chose a ete efface. Un fichier
   deja absent n'est pas une erreur — l'appelant voulait qu'il ne soit plus la —
   mais la valeur rendue est alors false, et on reproduit cela. */
inline bool remove(const std::filesystem::path &p)
{
    const bool was_there = dkr::fs::exists(p);
    return dkr_file_remove(p.string().c_str()) == DKR_FILE_OK && was_there;
}

inline bool remove(const std::filesystem::path &p, std::error_code &ec)
{
    ec.clear();
    return dkr::fs::remove(p);
}

/* `std::filesystem::create_directories` ne rend pas « le repertoire est la » mais
   « j'en ai cree au moins un » : sur un repertoire deja present elle rend
   **false**, sans que ce soit une erreur.

   La couche C, elle, traite le repertoire deja present comme un succes — ce qui
   est le bon contrat pour elle, l'appelant voulant que le chemin existe. Les
   deux sont justes, et c'est ici qu'ils se rejoignent : on regarde d'abord si le
   chemin etait la.

   Sans cela l'ecart etait invisible et pourtant reel — la suite d'epreuve le
   validait sur la machine parce qu'elle demandait seulement true, et un appelant
   qui compte les repertoires reellement crees aurait ete trompe. C'est le meme
   piege que `remove`, decrit en tete de la suite. */
inline bool create_directories(const std::filesystem::path &p)
{
    if (dkr::fs::exists(p)) {
        return false;
    }
    return dkr_file_create_directories(p.string().c_str()) == DKR_FILE_OK;
}

inline bool create_directories(const std::filesystem::path &p, std::error_code &ec)
{
    ec.clear();
    return dkr::fs::create_directories(p);
}

/* Seule la forme `overwrite_existing` est employee par le code appelant, et
   c'est la seule fournie : reproduire les autres options de `copy_options`
   serait ecrire du code que personne n'appelle. */
inline bool copy_file_overwrite(const std::filesystem::path &from,
                                const std::filesystem::path &to)
{
    return dkr_file_copy(from.string().c_str(), to.string().c_str())
           == DKR_FILE_OK;
}

inline bool copy_file_overwrite(const std::filesystem::path &from,
                                const std::filesystem::path &to,
                                std::error_code &ec)
{
    ec.clear();
    return dkr::fs::copy_file_overwrite(from, to);
}

/* `copy_options::none` — le defaut de `std::filesystem::copy_file` — refuse
   d'ecraser. Deux sites d'appel en dependent : importer un filtre ou un pack de
   textures ne doit pas remplacer celui qui porte deja ce nom. Les deux formes
   sont donc nommees, plutot que de faire passer un jeu d'options que personne
   n'emploie au-dela de ces deux valeurs. */
inline bool copy_file_no_overwrite(const std::filesystem::path &from,
                                   const std::filesystem::path &to)
{
    return dkr_file_copy_no_overwrite(from.string().c_str(),
                                      to.string().c_str()) == DKR_FILE_OK;
}

inline bool copy_file_no_overwrite(const std::filesystem::path &from,
                                   const std::filesystem::path &to,
                                   std::error_code &ec)
{
    ec.clear();
    if (dkr_file_copy_no_overwrite(from.string().c_str(), to.string().c_str())
        != DKR_FILE_OK) {
        ec = std::make_error_code(std::errc::file_exists);
        return false;
    }
    return true;
}

inline bool is_regular_file(const std::filesystem::path &p)
{
    return dkr_file_is_regular(p.string().c_str()) != 0;
}

inline bool is_regular_file(const std::filesystem::path &p, std::error_code &ec)
{
    ec.clear();
    return dkr::fs::is_regular_file(p);
}

inline std::uintmax_t file_size(const std::filesystem::path &p)
{
    int ok = 0;
    const unsigned long long n = dkr_file_size(p.string().c_str(), &ok);
    /* `std::filesystem::file_size` rend `-1` converti en `uintmax_t` quand elle
       echoue et qu'on lui a passe un `error_code`. On reproduit ce sentinelle
       plutot que zero : un fichier vide rend zero legitimement, et confondre les
       deux ferait prendre un echec pour un fichier vide. */
    return ok ? (std::uintmax_t)n : (std::uintmax_t)-1;
}

inline std::uintmax_t file_size(const std::filesystem::path &p, std::error_code &ec)
{
    const std::uintmax_t n = dkr::fs::file_size(p);
    if (n == (std::uintmax_t)-1) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
    } else {
        ec.clear();
    }
    return n;
}

inline void rename(const std::filesystem::path &from,
                   const std::filesystem::path &to)
{
    dkr_file_rename(from.string().c_str(), to.string().c_str());
}

inline void rename(const std::filesystem::path &from,
                   const std::filesystem::path &to, std::error_code &ec)
{
    ec.clear();
    if (dkr_file_rename(from.string().c_str(), to.string().c_str())
        != DKR_FILE_OK) {
        ec = std::make_error_code(std::errc::io_error);
    }
}

inline std::filesystem::path absolute(const std::filesystem::path &p)
{
    char out[512];
    if (dkr_file_absolute(out, sizeof(out), p.string().c_str()) != DKR_FILE_OK) {
        return p;                    /* mieux vaut le chemin d'origine que rien */
    }
    return std::filesystem::path{out};
}

/* La forme sans code d'erreur rend le chemin d'origine quand elle echoue, ce qui
   convient a un appelant qui veut juste « le meilleur chemin disponible ». Un
   autre veut savoir : `game_main` se rabat sur le repertoire courant quand le
   chemin de l'executable ne se resout pas. Rendre le chemin d'origine le
   priverait de cette decision. */
inline std::filesystem::path absolute(const std::filesystem::path &p,
                                      std::error_code &ec)
{
    char out[512];
    if (dkr_file_absolute(out, sizeof(out), p.string().c_str()) != DKR_FILE_OK) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return p;
    }
    ec.clear();
    return std::filesystem::path{out};
}

/* Le pendant de `directory_iterator`, rendu comme une liste. Voir `fileio.h` :
   reproduire un iterateur demanderait un cycle de vie et des categories dont
   aucun appelant ne se sert. */
inline std::vector<std::filesystem::path>
list_directory(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> out;
    dkr_dir *d = dkr_dir_open(dir.string().c_str());
    if (!d) {
        return out;
    }
    for (const char *name = dkr_dir_next(d); name; name = dkr_dir_next(d)) {
        out.push_back(dir / name);
    }
    dkr_dir_close(d);
    return out;
}

/* Un repertoire vide et un repertoire illisible rendent tous deux une liste
   vide, et un site d'appel les distingue pour le dire au joueur — « T.T. could
   not read this location ». D'ou cette forme.

   `dkr_dir_open` ne rend NULL que sur un echec reel : sur la cible, une
   recherche dans un repertoire valide trouve toujours au moins « . » et
   « .. », meme s'il est vide. */
inline std::vector<std::filesystem::path>
list_directory(const std::filesystem::path &dir, std::error_code &ec)
{
    std::vector<std::filesystem::path> out;
    dkr_dir *d = dkr_dir_open(dir.string().c_str());
    if (!d) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return out;
    }
    ec.clear();
    for (const char *name = dkr_dir_next(d); name; name = dkr_dir_next(d)) {
        out.push_back(dir / name);
    }
    dkr_dir_close(d);
    return out;
}

inline std::uintmax_t remove_all(const std::filesystem::path &p)
{
    unsigned long long n = 0;
    dkr_file_remove_all(p.string().c_str(), &n);
    return (std::uintmax_t)n;
}

inline std::uintmax_t remove_all(const std::filesystem::path &p,
                                 std::error_code &ec)
{
    unsigned long long n = 0;
    ec.clear();
    if (dkr_file_remove_all(p.string().c_str(), &n) != DKR_FILE_OK) {
        ec = std::make_error_code(std::errc::io_error);
    }
    return (std::uintmax_t)n;
}

inline std::filesystem::path current_path()
{
    char out[512];
    if (dkr_file_current_directory(out, sizeof(out)) != DKR_FILE_OK) {
        return std::filesystem::path{};
    }
    return std::filesystem::path{out};
}

inline std::filesystem::path temp_directory_path()
{
    char out[512];
    if (dkr_file_temp_directory(out, sizeof(out)) != DKR_FILE_OK) {
        return std::filesystem::path{};
    }
    return std::filesystem::path{out};
}

inline std::filesystem::path current_path(std::error_code &ec)
{
    char out[512];
    if (dkr_file_current_directory(out, sizeof(out)) != DKR_FILE_OK) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return std::filesystem::path{};
    }
    ec.clear();
    return std::filesystem::path{out};
}

/* **Windows 95 n'a pas de liens symboliques.** Ni les jonctions de NTFS, qui
   n'arrivent qu'avec Windows 2000, ni les liens de Vista.

   Rendre false n'est donc pas un raccourci ni un aveu d'impuissance : c'est la
   reponse juste sur cette plate-forme. Le site d'appel qui refuse d'effacer
   recursivement un lien symbolique garde tout son sens ailleurs, et ici il ne
   peut simplement jamais se declencher. */
inline bool is_symlink(const std::filesystem::path &)
{
    return false;
}

/* `weakly_canonical` resout « . » et « .. » et rend un chemin absolu, sans
   exiger que le chemin existe. `GetFullPathNameA` fait exactement cela — c'est
   d'ailleurs plus proche de `weakly_canonical` que ne l'est
   `std::filesystem::absolute`, qui se contente de prefixer le repertoire
   courant. Ce qui manque est la resolution des liens symboliques, et il n'y en
   a pas ici. */
inline std::filesystem::path weakly_canonical(const std::filesystem::path &p)
{
    return dkr::fs::absolute(p);
}

#else

/* Sur toute autre cible, ce sont les fonctions de la bibliotheque standard, sans
   la moindre couche entre elles et l'appelant.
 *
 * Le controleur de sous-ensemble surveille les operations de `std::filesystem`,
 * et il a raison de les voir ici : ce sont bien elles. Mais cette branche est
 * celle des cibles modernes, que Windows 95 ne compile jamais — la derogation
 * porte donc sur chaque ligne, avec son motif, comme pour le pont de fils. */
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::create_directories;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::exists;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::is_directory;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::remove;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::is_regular_file;

/* Ces trois-la ne sont pas reprises telles quelles, et la raison vaut d'etre
 * dite : `std::filesystem::file_size`, `rename` et `absolute` **levent** quand
 * on ne leur passe pas de code d'erreur, la ou la branche Windows 95 ne le peut
 * pas — elle rend un sentinelle.
 *
 * Deux branches d'un meme point d'indirection qui different sur la gestion des
 * erreurs sont pires que pas de point d'indirection du tout : le code marche sur
 * l'hote et se comporte autrement sur la cible, ce qui est exactement ce qu'un
 * portage doit eviter. Elles sont donc enveloppees pour ne **jamais** lever, des
 * deux cotes.
 *
 * Aucun site d'appel n'y perd : tous emploient deja la forme a `error_code`. */
inline std::uintmax_t file_size(const std::filesystem::path &p)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    const std::uintmax_t n = std::filesystem::file_size(p, ec);
    return ec ? (std::uintmax_t)-1 : n;
}

inline std::uintmax_t file_size(const std::filesystem::path &p, std::error_code &ec)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::file_size(p, ec);
}

inline void rename(const std::filesystem::path &from,
                   const std::filesystem::path &to)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    std::filesystem::rename(from, to, ec);
}

inline void rename(const std::filesystem::path &from,
                   const std::filesystem::path &to, std::error_code &ec)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    std::filesystem::rename(from, to, ec);
}

inline std::filesystem::path absolute(const std::filesystem::path &p)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    const std::filesystem::path a = std::filesystem::absolute(p, ec);
    return ec ? p : a;
}

inline std::filesystem::path absolute(const std::filesystem::path &p,
                                      std::error_code &ec)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::absolute(p, ec);
}

inline std::vector<std::filesystem::path>
list_directory(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    /* `skip_permission_denied` reproduit ce que faisaient les sites d'appel : un
       repertoire illisible fait sauter l'entree, pas la boucle. Windows 95 n'a
       pas de permissions au sens ou l'entend cette option, et sa branche se
       comporte deja ainsi. */
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    for (const auto &entry : std::filesystem::directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied,
             ec)) {
        out.push_back(entry.path());
    }
    return out;
}

inline std::vector<std::filesystem::path>
list_directory(const std::filesystem::path &dir, std::error_code &ec)
{
    std::vector<std::filesystem::path> out;
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    for (const auto &entry : std::filesystem::directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied,
             ec)) {
        out.push_back(entry.path());
    }
    return out;
}

inline bool copy_file_overwrite(const std::filesystem::path &from,
                                const std::filesystem::path &to)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::overwrite_existing);
}

inline bool copy_file_no_overwrite(const std::filesystem::path &from,
                                   const std::filesystem::path &to)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::none);
}

inline bool copy_file_no_overwrite(const std::filesystem::path &from,
                                   const std::filesystem::path &to,
                                   std::error_code &ec)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::none, ec);
}

// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::remove_all;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::current_path;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::temp_directory_path;
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
using std::filesystem::is_symlink;

inline std::filesystem::path weakly_canonical(const std::filesystem::path &p)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    const std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : c;
}

inline bool copy_file_overwrite(const std::filesystem::path &from,
                                const std::filesystem::path &to,
                                std::error_code &ec)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::overwrite_existing, ec);
}

#endif

} // namespace dkr::fs

#endif /* DKR_WIN95_FILEIO_HPP */
