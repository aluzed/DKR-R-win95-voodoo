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

#include <filesystem>
#include <system_error>

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

inline bool create_directories(const std::filesystem::path &p)
{
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

inline bool copy_file_overwrite(const std::filesystem::path &from,
                                const std::filesystem::path &to)
{
    // DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::overwrite_existing);
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
