/* E02-S02 - indirection point for the synchronisation primitives.
 *
 * The same shape as `fileio.hpp`, and for the same reason: the calling code
 * keeps its habits, and only name resolution changes with the target.
 *
 *   on Windows 95   `dkr::sync::mutex` is E02-S01's, built on CRITICAL_SECTION
 *                   and _beginthreadex
 *   elsewhere       these are the standard library's types, with not a single
 *                   layer between them and the caller
 *
 * The difference from the file system is worth noting. There, the **type**
 * `std::filesystem::path` worked and only its operations were a problem; here it
 * is the reverse: it is the types themselves that do not pass, because including
 * `<mutex>` or `<thread>` brings symbols Windows 95 does not export into the
 * import table - and the binary then stops loading. The name therefore has to
 * change at the declaration sites, which touches more code, with no choice in
 * the matter.
 *
 * `<chrono>` does work: durations pass through unchanged.
 *
 * What is supplied is what the game's sources use, and nothing more:
 *
 *     scoped_lock   95 uses, all on a single lock
 *     mutex         11
 *     lock_guard     3
 *     thread         1
 *     sleep_for      1
 *
 * `condition_variable` and `unique_lock` come with them, because the bridge
 * already carries them for `ultramodern`.
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

/* The subset checker watches these two headers, and it is right to see them
 * here: these are indeed them. But this branch is the modern targets' branch,
 * which Windows 95 never compiles - so the waiver is granted line by line, with
 * its reason, as for `fileio.hpp`. */
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
#include <mutex>
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
#include <thread>
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
#include <condition_variable>

namespace dkr::sync {

// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::condition_variable;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::cv_status;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::lock_guard;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::mutex;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::scoped_lock;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::thread;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::unique_lock;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
namespace this_thread = std::this_thread;

#endif

} // namespace dkr::sync

#endif /* DKR_WIN95_SYNC_HPP */
