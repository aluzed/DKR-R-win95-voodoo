/* E02-S05 - indirection point for the file-system operations.
 *
 * `std::filesystem::path` **works** under Windows 95: the header costs nothing,
 * the type pulls in only one stub, and manipulating it has been verified on the
 * machine - construction, `parent_path`, `filename`, `extension`, concatenation.
 * It is string manipulation, and it asks nothing of the system. See
 * `docs/research/win95-filesystem.md`.
 *
 * It is its **operations** that do not pass: `exists` alone asks for seventeen
 * symbols, seven of which Windows 95 does not export at all - and the binary
 * then stops loading.
 *
 * The consequence for this file's shape is important, and it is what
 * distinguishes it from an ordinary abstraction layer: **the type does not
 * change**. The signatures keep `std::filesystem::path`, the type's 250 uses
 * stay intact, and only the operation calls are diverted. A port that had
 * replaced the type would have touched five times as much code for no gain, and
 * made the modern target diverge from the oracle.
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

/* Paths are converted with `string()` - that is, to narrow bytes - and not with
   `wstring()`. Under Windows 9x the `...W` family is a stub (E01-S03), and the
   layer below therefore only calls the `...A` ones. */
inline bool exists(const std::filesystem::path &p)
{
    return dkr_file_exists(p.string().c_str()) != 0;
}

inline bool exists(const std::filesystem::path &p, std::error_code &ec)
{
    ec.clear();
    /* Qualified: the argument being a `std::filesystem::path`, argument-dependent
       lookup would find `std::filesystem::exists` and the call would be
       ambiguous. */
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

/* `std::filesystem::remove` returns true if something was deleted. An already
   absent file is not an error - the caller wanted it gone - but the returned
   value is then false, and we reproduce that. */
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

/* `std::filesystem::create_directories` does not return "the directory is
   there" but "I created at least one": on an already present directory it
   returns **false**, without that being an error.

   The C layer, for its part, treats an already present directory as a success -
   which is the right contract for it, the caller wanting the path to exist. Both
   are right, and this is where they meet: we first look at whether the path was
   there.

   Without this the gap was invisible and nonetheless real - the test suite
   validated it on the machine because it only asked for true, and a caller
   counting the directories actually created would have been misled. It is the
   same trap as `remove`, described at the top of the suite. */
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

/* Only the `overwrite_existing` form is used by the calling code, and it is the
   only one supplied: reproducing the other `copy_options` would be writing code
   nobody calls. */
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

/* `copy_options::none` - `std::filesystem::copy_file`'s default - refuses to
   overwrite. Two call sites depend on it: importing a filter or a texture pack
   must not replace the one that already bears that name. Both forms are
   therefore named, rather than passing an options set nobody uses beyond these
   two values. */
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
    /* `std::filesystem::file_size` returns `-1` converted to `uintmax_t` when it
       fails and was given an `error_code`. We reproduce that sentinel rather than
       zero: an empty file legitimately returns zero, and conflating the two would
       make a failure look like an empty file. */
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
        return p;                    /* the original path beats nothing */
    }
    return std::filesystem::path{out};
}

/* The form without an error code returns the original path when it fails, which
   suits a caller that just wants "the best path available". Another wants to
   know: `game_main` falls back to the current directory when the executable's
   path does not resolve. Returning the original path would deprive it of that
   decision. */
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

/* The counterpart of `directory_iterator`, returned as a list. See `fileio.h`:
   reproducing an iterator would demand a life cycle and categories no caller
   uses. */
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

/* An empty directory and an unreadable directory both return an empty list, and
   one call site tells them apart in order to say so to the player - "T.T. could
   not read this location". Hence this form.

   `dkr_dir_open` only returns NULL on a real failure: on the target, a search in
   a valid directory always finds at least "." and "..", even when it is
   empty. */
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

/* **Windows 95 has no symbolic links.** Neither NTFS junctions, which only
   arrive with Windows 2000, nor Vista's links.

   Returning false is therefore neither a shortcut nor an admission of defeat: it
   is the right answer on this platform. The call site that refuses to delete a
   symbolic link recursively keeps all its meaning elsewhere, and here it simply
   can never fire. */
inline bool is_symlink(const std::filesystem::path &)
{
    return false;
}

/* `weakly_canonical` resolves "." and ".." and returns an absolute path, without
   requiring the path to exist. `GetFullPathNameA` does exactly that - it is in
   fact closer to `weakly_canonical` than `std::filesystem::absolute` is, the
   latter merely prefixing the current directory. What is missing is symbolic
   link resolution, and there are none here. */
inline std::filesystem::path weakly_canonical(const std::filesystem::path &p)
{
    return dkr::fs::absolute(p);
}

#else

/* On every other target, these are the standard library's functions, with not a
   single layer between them and the caller.
 *
 * The subset checker watches `std::filesystem`'s operations, and it is right to
 * see them here: these are indeed them. But this branch is the modern targets'
 * branch, which Windows 95 never compiles - so the waiver is granted line by
 * line, with its reason, as for the threading bridge. */
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::create_directories;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::exists;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::is_directory;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::remove;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::is_regular_file;

/* These three are not taken over as they are, and the reason is worth stating:
 * `std::filesystem::file_size`, `rename` and `absolute` **throw** when they are
 * not given an error code, where the Windows 95 branch cannot - it returns a
 * sentinel.
 *
 * Two branches of the same indirection point that differ on error handling are
 * worse than no indirection point at all: the code works on the host and behaves
 * differently on the target, which is exactly what a port must avoid. They are
 * therefore wrapped so as **never** to throw, on either side.
 *
 * No call site loses anything: all already use the `error_code` form. */
inline std::uintmax_t file_size(const std::filesystem::path &p)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    const std::uintmax_t n = std::filesystem::file_size(p, ec);
    return ec ? (std::uintmax_t)-1 : n;
}

inline std::uintmax_t file_size(const std::filesystem::path &p, std::error_code &ec)
{
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    return std::filesystem::file_size(p, ec);
}

inline void rename(const std::filesystem::path &from,
                   const std::filesystem::path &to)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    std::filesystem::rename(from, to, ec);
}

inline void rename(const std::filesystem::path &from,
                   const std::filesystem::path &to, std::error_code &ec)
{
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    std::filesystem::rename(from, to, ec);
}

inline std::filesystem::path absolute(const std::filesystem::path &p)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    const std::filesystem::path a = std::filesystem::absolute(p, ec);
    return ec ? p : a;
}

inline std::filesystem::path absolute(const std::filesystem::path &p,
                                      std::error_code &ec)
{
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    return std::filesystem::absolute(p, ec);
}

inline std::vector<std::filesystem::path>
list_directory(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    /* `skip_permission_denied` reproduces what the call sites did: an unreadable
       directory skips the entry, not the loop. Windows 95 has no permissions in
       the sense this option means, and its branch already behaves this way. */
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
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
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
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
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::overwrite_existing);
}

inline bool copy_file_no_overwrite(const std::filesystem::path &from,
                                   const std::filesystem::path &to)
{
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::none);
}

inline bool copy_file_no_overwrite(const std::filesystem::path &from,
                                   const std::filesystem::path &to,
                                   std::error_code &ec)
{
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::none, ec);
}

// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::remove_all;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::current_path;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::temp_directory_path;
// DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
using std::filesystem::is_symlink;

inline std::filesystem::path weakly_canonical(const std::filesystem::path &p)
{
    std::error_code ec;
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    const std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    return ec ? p : c;
}

inline bool copy_file_overwrite(const std::filesystem::path &from,
                                const std::filesystem::path &to,
                                std::error_code &ec)
{
    // DKR-WIN95-ALLOW: modern-target branch, never compiled on Windows 95
    return std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::overwrite_existing, ec);
}

#endif

} // namespace dkr::fs

#endif /* DKR_WIN95_FILEIO_HPP */
