/* E02-S02 — point d'indirection des primitives de synchronisation.
 *
 * Meme forme que `fileio.hpp`, et pour la meme raison : le code appelant garde
 * ses habitudes, et seule la resolution des noms change selon la cible.
 *
 *   sur Windows 95   `dkr::sync::mutex` est celui de E02-S01, bati sur
 *                    CRITICAL_SECTION et _beginthreadex
 *   ailleurs         ce sont les types de la bibliotheque standard, sans la
 *                    moindre couche entre eux et l'appelant
 *
 * La difference avec le systeme de fichiers vaut d'etre notee. La, le **type**
 * `std::filesystem::path` fonctionnait et seules ses operations posaient
 * probleme ; ici c'est l'inverse : ce sont les types eux-memes qui ne passent
 * pas, parce que l'inclusion de `<mutex>` ou de `<thread>` fait entrer dans la
 * table d'imports des symboles que Windows 95 n'exporte pas — et le binaire
 * cesse alors de se charger. Le nom doit donc changer aux points de
 * declaration, ce qui touche plus de code, sans qu'il y ait de choix.
 *
 * `<chrono>`, lui, fonctionne : les durees passent inchangees.
 *
 * Ce qui est fourni est ce que les sources du jeu emploient, et rien de plus :
 *
 *     scoped_lock   95 emplois, tous sur un seul verrou
 *     mutex         11
 *     lock_guard     3
 *     thread         1
 *     sleep_for      1
 *
 * `condition_variable` et `unique_lock` viennent avec, parce que le pont les
 * porte deja pour `ultramodern`.
 */
#ifndef DKR_WIN95_SYNC_HPP
#define DKR_WIN95_SYNC_HPP

namespace dkr::sync {

#if defined(DKR_TARGET_WIN95)

} // namespace dkr::sync

#include "win95/threading.hpp"

namespace dkr::sync {

using dkr::win95::condition_variable;
using dkr::win95::cv_status;
using dkr::win95::lock_guard;
using dkr::win95::mutex;
using dkr::win95::scoped_lock;
using dkr::win95::thread;
using dkr::win95::unique_lock;
namespace this_thread = dkr::win95::this_thread;

#else

} // namespace dkr::sync

/* Le controleur de sous-ensemble surveille ces deux en-tetes, et il a raison de
 * les voir ici : ce sont bien elles. Mais cette branche est celle des cibles
 * modernes, que Windows 95 ne compile jamais — la derogation porte donc sur
 * chaque ligne, avec son motif, comme pour `fileio.hpp`. */
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
#include <mutex>
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
#include <thread>
// DKR-WIN95-ALLOW: branche des cibles modernes, jamais compilee sur Windows 95
#include <condition_variable>

namespace dkr::sync {

using std::condition_variable;
using std::cv_status;
using std::lock_guard;
using std::mutex;
using std::scoped_lock;
using std::thread;
using std::unique_lock;
namespace this_thread = std::this_thread;

#endif

} // namespace dkr::sync

#endif /* DKR_WIN95_SYNC_HPP */
