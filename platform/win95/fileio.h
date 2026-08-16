/* E02-S05 - writing save files under Windows 95.
 *
 * "Losing a player's progress is a port's least forgivable defect." On a 1998
 * machine with no UPS, power loss during a write is not an edge case: it is a
 * common scenario. Everything in this file follows from that.
 *
 * ## What the machine allows, measured and not assumed
 *
 * Measured by `tools/win95/witnesses/fileio_probe.cpp`; details in
 * `docs/research/win95-fileio.md`.
 *
 *   MoveFileExA(REPLACE_EXISTING)   FAILS - error 120, not implemented
 *   MoveFileA onto an existing target refused, as documented
 *   long names on the FAT16 volume  work (VFAT), 8.3 alias readable
 *   GetModuleFileNameA              gives the executable's directory
 *   writing to absent media         refused cleanly, usable error
 *
 * The first point governs everything: **there is no atomic replacement**. The
 * ticket announced it and measurement confirms it - unlike two of its other
 * assumptions, contradicted by E02-S03. The sequence therefore has to be written
 * by hand, and its window of vulnerability owned.
 *
 * ## The sequence, and what it guarantees
 *
 *   1. write     SAVE.TMP, flush the buffers, close
 *   2. delete    SAVE.BAK
 *   3. rename    SAVE.DAT -> SAVE.BAK
 *   4. rename    SAVE.TMP -> SAVE.DAT
 *
 * Between 3 and 4, `SAVE.DAT` does not exist. A power cut at that moment leaves
 * the previous save in `SAVE.BAK` and the new, complete one in `SAVE.TMP`.
 *
 * **The guarantee offered is therefore not "we never lose the last write" but
 * "we never lose a valid save".** That is the distinction that counts: losing
 * the last race is annoying, losing the whole of a player's progress is
 * unforgivable.
 *
 * On read-back, the order of preference is therefore:
 *
 *   SAVE.DAT   if it exists - the write ran to completion
 *   SAVE.BAK   otherwise - the previous one, known good
 *   SAVE.TMP   never: nothing proves it is complete, and DKR-R's format carries
 *              no checksum that would allow checking it without modifying it
 */
#ifndef DKR_WIN95_FILEIO_H
#define DKR_WIN95_FILEIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes. Told apart because the caller must be able to tell the player
   *why* their save failed - "disk full" and "write-protected medium" do not call
   for the same action. */
typedef enum {
    DKR_FILE_OK = 0,
    DKR_FILE_ERR_PATH,        /* invalid or over-long path */
    DKR_FILE_ERR_NOT_FOUND,   /* nothing to read */
    DKR_FILE_ERR_ACCESS,      /* read-only medium, locked file */
    DKR_FILE_ERR_NO_SPACE,    /* disk full */
    DKR_FILE_ERR_NOT_READY,   /* drive empty or absent */
    DKR_FILE_ERR_IO           /* everything else */
} dkr_file_result;

const char *dkr_file_result_text(dkr_file_result r);

/* --- Location ------------------------------------------------------------- *
 *
 * There is no `%APPDATA%` under Windows 95, and the application's own folder is
 * the practice of the era. A program launched from the Start menu inherits a
 * current directory that has nothing to do with where it is installed:
 * `GetModuleFileNameA` is the authority, never the current directory.
 */
dkr_file_result dkr_file_app_directory(char *out, size_t out_size);

/* Joins `directory` and `name` with the appropriate separator. */
dkr_file_result dkr_file_join(char *out, size_t out_size,
                              const char *directory, const char *name);

/* --- Durable writing ------------------------------------------------------- *
 *
 * Applies the sequence described at the top of the file. `path` is the final
 * name; the `.TMP` and `.BAK` files are derived from it.
 *
 * `flush` empties the buffers before closing. Leaving it at zero makes the write
 * faster and unguaranteed - which only makes sense for a file whose loss has no
 * consequence.
 */
dkr_file_result dkr_file_write_durable(const char *path,
                                       const void *data, size_t size);

/* Reads the file applying the order of preference described above. `*read_size`
   receives the size read. If `from_backup` is non-null, it receives 1 when the
   backup copy was used - the caller can then tell the player rather than let
   them discover that they have lost their last race. */
dkr_file_result dkr_file_read_durable(const char *path,
                                      void *buffer, size_t buffer_size,
                                      size_t *read_size, int *from_backup);

/* --- File-system operations ----------------------------------------------- *
 *
 * `std::filesystem` already supplies all of this, and both its header and its
 * `path` type are usable under Windows 95 - measured, see
 * `docs/research/win95-filesystem.md`. It is its **operations** that are not:
 * `exists` alone asks for seventeen symbols, seven of which the system does not
 * export at all, and the binary then stops loading.
 *
 * The ones below are exactly those `librecomp` uses outside the mod system. They
 * do not try to reproduce `std::filesystem`: they cover what is called, and
 * nothing more.
 */

/* Returns 1 if the path exists, 0 otherwise. Does not tell "absent" from
   "inaccessible" - which is what `std::filesystem::exists` does with an error
   code, and the callers are content with it. */
int dkr_file_exists(const char *path);

/* Returns 1 if the path exists and is a directory. */
int dkr_file_is_directory(const char *path);

/* Deletes a file. An already absent file is a success, as with
   `std::filesystem::remove`: the caller wanted it gone. */
dkr_file_result dkr_file_remove(const char *path);

/* Creates the directory and all its parents. An already present directory is a
   success. */
dkr_file_result dkr_file_create_directories(const char *path);

/* Copies, overwriting the destination if it exists - that is,
   `copy_options::overwrite_existing`, the only form in use. */
dkr_file_result dkr_file_copy(const char *from, const char *to);

/* Copies while **refusing** to overwrite: that is `copy_options::none`,
   `std::filesystem::copy_file`'s default, and two call sites depend on it -
   importing a filter or a texture pack must not silently replace the one that
   already bears that name.

   The refusal is done by the system and not by a prior `exists`: between the
   test and the copy there is an interval, however small, and `CopyFileA` can
   refuse on its own. */
dkr_file_result dkr_file_copy_no_overwrite(const char *from, const char *to);

/* Returns 1 if the path exists and is an ordinary file. */
int dkr_file_is_regular(const char *path);

/* Size in bytes. Returns 0 and sets `*ok` to 0 if the path is not readable - an
   empty file and an absent file both return 0, hence the flag. */
unsigned long long dkr_file_size(const char *path, int *ok);

/* Renames, overwriting the destination if it exists. `MoveFileA` refuses to
   overwrite and `MoveFileExA` is not implemented under Windows 95: so we delete
   first, with the same window of vulnerability as the durable write and for the
   same reason. */
dkr_file_result dkr_file_rename(const char *from, const char *to);

/* Returns the absolute path in `out`. Under Windows 95 that is
   `GetFullPathNameA`, which also resolves `..` - where
   `std::filesystem::absolute` merely prefixes the current directory. The
   difference is written down rather than hidden: it matters if a caller compares
   two paths textually. */
dkr_file_result dkr_file_absolute(char *out, size_t out_size, const char *path);

/* Deletes a path and everything it contains. `*removed` receives the number of
   entries actually deleted - that is what `std::filesystem::remove_all` returns,
   and a caller can use it to say what it did.

   There is no atomic version: a power cut mid-delete leaves a partial tree. That
   is true of `std::filesystem::remove_all` too, and the callers use it for
   working directories. */
dkr_file_result dkr_file_remove_all(const char *path, unsigned long long *removed);

/* The current directory. A reminder of the warning above: under Windows 95 a
   program launched from the Start menu inherits a current directory that has
   nothing to do with where it lives. To find where we are installed, it is
   `dkr_file_app_directory` that is needed, never this one. */
dkr_file_result dkr_file_current_directory(char *out, size_t out_size);

/* The temporary-files directory. `GetTempPathA` consults TMP, then TEMP, then
   the Windows directory - and always returns something. Its `...W` variant is a
   stub, like the rest of the family. */
dkr_file_result dkr_file_temp_directory(char *out, size_t out_size);

/* --- Enumerating a directory ---------------------------------------------- *
 *
 * `std::filesystem::directory_iterator` is not reproduced: an iterator demands a
 * life cycle, categories, comparators, and the calling code uses only one thing
 * of it - walking the entries once. So we return the list, which can be tested
 * and read.
 *
 * `dkr_dir_open` returns NULL if the directory does not exist. Each call to
 * `dkr_dir_next` returns the next entry's name, without the path, or NULL at the
 * end; "." and ".." are skipped, as `directory_iterator` does.
 */
typedef struct dkr_dir dkr_dir;

dkr_dir    *dkr_dir_open(const char *path);
const char *dkr_dir_next(dkr_dir *d);
void        dkr_dir_close(dkr_dir *d);

/* --- File names ------------------------------------------------------------ *
 *
 * The test volume is FAT16 with long names active, and a long name survives the
 * round trip there. But nothing guarantees that is true everywhere: a
 * first-generation Windows 95 without VFAT, or a volume mounted differently,
 * falls back to 8.3.
 *
 * This function fixes nothing - it **answers**, so that the caller can decide
 * knowingly rather than discover the truncation afterwards. Returns 1 if `name`
 * fits in strict 8.3. */
int dkr_file_name_is_8dot3(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_FILEIO_H */
