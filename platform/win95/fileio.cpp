/* E02-S05 - writing save files under Windows 95.
 *
 * The contract, the write sequence and above all what it guarantees - and what
 * it does not - are in `fileio.h`. This file contains only the implementation.
 *
 * Like the other layers in `platform/win95`, it carries a Windows
 * implementation, the target, and a POSIX vehicle that exists only so that the
 * suite also runs on the host. The durable sequence itself is shared: it is what
 * has to be tested, and two copies would end up diverging.
 */
#include "fileio.h"

#include <string.h>

/* ========================================================================== *
 * Shared
 * ========================================================================== */

const char *dkr_file_result_text(dkr_file_result r)
{
    switch (r) {
    case DKR_FILE_OK:            return "success";
    case DKR_FILE_ERR_PATH:      return "invalid or over-long path";
    case DKR_FILE_ERR_NOT_FOUND: return "file not found";
    case DKR_FILE_ERR_ACCESS:    return "access denied (write-protected medium?)";
    case DKR_FILE_ERR_NO_SPACE:  return "disk full";
    case DKR_FILE_ERR_NOT_READY: return "drive empty or absent";
    default:                     return "input/output error";
    }
}

/* Strict 8.3: at most eight characters, an optional dot, at most three. None of
   the characters FAT refuses. We answer, we do not correct. */
int dkr_file_name_is_8dot3(const char *name)
{
    const char *dot;
    size_t base_len, ext_len, i;

    if (!name || !*name) {
        return 0;
    }
    /* The characters FAT refuses, plus the space, which DOS handles badly. */
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
    /* One dot only: `SAVE.DAT.BAK` is not 8.3. */
    if (strchr(dot + 1, '.') != NULL) {
        return 0;
    }
    base_len = (size_t)(dot - name);
    ext_len  = strlen(dot + 1);
    return base_len >= 1 && base_len <= 8 && ext_len <= 3;
}

/* Derives the auxiliary names from the final name. We replace the extension
   rather than add one: `SAVE.DAT.TMP` would not be 8.3, and the target volume
   may accept nothing else. */
static int derive_sibling(char *out, size_t out_size,
                          const char *path, const char *extension)
{
    const char *dot;
    size_t base_len;

    if (!out || !path || out_size == 0) {
        return 0;
    }
    dot = strrchr(path, '.');
    /* A dot in a parent directory does not count. */
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
 * Windows - the target
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
    /* `GetModuleFileNameA` and never the current directory: launched from the
       Start menu, a program inherits a current directory unrelated to where it
       is installed. Measured on the machine: the executable was in D:\ and so
       was the current directory, but that is a coincidence of the test protocol,
       not a property. */
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
        /* A short write on a full disk does not always report an error: the
           byte count, on the other hand, does not lie. */
        CloseHandle(h);
        return DKR_FILE_ERR_NO_SPACE;
    }
    /* Flush before closing. Without it, "the file is written" means nothing: the
       data is in the system's cache, and it is precisely the cache that a power
       cut carries away. */
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

    /* 1. The new version, complete and on the disk. */
    r = write_whole(tmp, data, size);
    if (r != DKR_FILE_OK) {
        return r;
    }

    /* 2 and 3. The previous one becomes the backup copy. `MoveFileA` refuses to
       overwrite - measured on the machine - hence the prior delete. And
       `MoveFileExA(REPLACE_EXISTING)`, which would avoid both, fails with
       ERROR_CALL_NOT_IMPLEMENTED under Windows 95: it is exported, it has real
       code, and it refuses. */
    if (file_exists(path)) {
        DeleteFileA(bak);                    /* absent: no matter */
        if (!MoveFileA(path, bak)) {
            r = from_last_error();
            DeleteFileA(tmp);
            return r;
        }
    }

    /* 4. And the new one takes its place. This is where the window is: between 3
       and 4 the final file does not exist. A power cut at that moment leaves the
       previous one in .BAK and the new, complete one in .TMP - and read-back
       prefers the first, because nothing proves the second. */
    if (!MoveFileA(tmp, path)) {
        r = from_last_error();
        /* The old one has already been moved: put it back, otherwise the last
           step's failure would have destroyed a valid save. */
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
    /* The directory case comes **first**, and the order is not cosmetic. This
       code tried `DeleteFileA` first and then fell back on `RemoveDirectoryA`;
       it returned DKR_FILE_OK on a directory without deleting anything, because
       `DeleteFileA`'s failure on a directory went through the "already absent"
       branch. The caller believed it had deleted, the directory stayed, and the
       test suite's prior cleanup did nothing - that is how the defect showed
       itself.

       Asking for the type before acting costs one call and removes the
       possibility of conflating "nothing to do" with "I could not". */
    if (dkr_file_is_directory(path)) {
        return RemoveDirectoryA(path) ? DKR_FILE_OK : from_last_error();
    }
    if (DeleteFileA(path)) {
        return DKR_FILE_OK;
    }
    /* Already absent: the caller wanted it gone, and it is gone. */
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

    /* We create each level, shortest to longest. `CreateDirectoryA` only creates
       one level at a time - there is no equivalent of `create_directories` under
       Windows 95. */
    for (i = 0; i <= len; i++) {
        const int at_end = (i == len);
        if (!at_end && work[i] != '\\' && work[i] != '/') {
            continue;
        }
        if (i == 0) {
            continue;                       /* leading separator */
        }
        {
            const char saved = work[i];
            work[i] = '\0';
            /* "D:" is not a directory to create, it is a volume. */
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
    /* FALSE: overwrite if the destination exists, which is
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
    /* TRUE: fail if the destination exists. */
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
    /* `GetFileAttributesExA` would be the natural choice, and it is what this
       code did first. **Windows 95 does not export it** - not a stub, an
       absence, and the loader then refuses to start the whole process. The
       import check stopped it before the machine did; it is exactly the kind of
       symbol Windows 98 added and that one assumes is there. `GetFileSizeEx` is
       absent for the same reason.
     *
       `FindFirstFileA` returns the size without opening the file - hence with no
       handle to leak and no sharing conflict with an already open file, which
       was the reason for the initial choice. It expects a literal path: a caller
       that slipped a `*` into it would measure a different entry. None does, and
       the function's name does not suggest otherwise. */
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
    /* `MoveFileA` refuses an existing target and `MoveFileExA` is not
       implemented here - E02-S05's measurement. So we delete first, which opens
       the same window as the durable write: between the delete and the rename,
       neither the old nor the new is in place. */
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

dkr_file_result dkr_file_temp_directory(char *out, size_t out_size)
{
    DWORD n;
    size_t len;
    if (!out || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    n = GetTempPathA((DWORD)out_size, out);
    if (n == 0 || n >= out_size) {
        return DKR_FILE_ERR_PATH;
    }
    /* `GetTempPathA` ends with a backslash, where
       `std::filesystem::temp_directory_path` does not. Without this a
       `path / "x"` would produce a double separator, and the two branches would
       return different paths for the same directory. */
    len = strlen(out);
    if (len > 1 && (out[len - 1] == '\\' || out[len - 1] == '/')) {
        out[len - 1] = '\0';
    }
    return DKR_FILE_OK;
}

struct dkr_dir {
    HANDLE           handle;
    WIN32_FIND_DATAA data;
    int              pending;   /* an already read entry waits to be returned */
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
    /* `FindFirstFileA` wants a pattern, not a directory. */
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
        /* "." and ".." are skipped, as `directory_iterator` does. */
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
 * POSIX - a test vehicle, not a supported platform
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
    /* On the host, only the durable sequence's behaviour interests us; the
       location is the current directory. */
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
    /* The equivalent of FlushFileBuffers: `fflush` only goes down to the system,
       `fsync` down to the disk. */
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

/* "wx" refuses an existing destination, and it is the system that decides - like
   CopyFileA's TRUE on the target. */
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
    /* We reproduce the target's sequence - delete then rename - rather than use
       POSIX's `rename`, which overwrites on its own. A test vehicle that takes a
       shortcut the target does not have is not testing the target. */
    if (dkr_file_exists(to)) {
        unlink(to);
    }
    if (rename(from, to) != 0) {
        return from_errno();
    }
    return DKR_FILE_OK;
}

/* Resolves "." and ".." in place, without touching the disk.
 *
 * `GetFullPathNameA` does it for the target; with no equivalent here, the test
 * backend would behave differently from the platform it serves to test, and the
 * test would prove nothing. It is the new arrangement - the Windows 95 branch
 * compiled on the host - that showed it: `weakly_canonical` returned two
 * different paths there for the same file.
 *
 * The resolution is purely lexical, like Windows': it does not follow symbolic
 * links and does not require the path to exist. That is exactly what
 * `weakly_canonical` asks for, and `realpath` would do too much. */
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
            /* nothing: a double separator or "." says nothing */
        } else if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            /* Go up: erase the last written segment. At the root, ".." leads
               nowhere and is ignored - as under Windows. */
            char *base = p + (absolute ? 1 : 0);
            if (out > base) {
                out--;                                   /* the trailing '/' */
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
    /* Remove the trailing separator, unless it is the root on its own. */
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

/* POSIX's `rename` overwrites, where `MoveFileA` refuses. We reproduce the
   target's sequence all the same - delete then rename - because it is **that**
   which the suite has to test. A test vehicle that took a shortcut the target
   does not have would not be testing the target. */
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

dkr_file_result dkr_file_temp_directory(char *out, size_t out_size)
{
    const char *tmp = getenv("TMPDIR");
    if (!out || out_size == 0) {
        return DKR_FILE_ERR_PATH;
    }
    if (!tmp || !*tmp) { tmp = "/tmp"; }
    if (strlen(tmp) + 1 > out_size) {
        return DKR_FILE_ERR_PATH;
    }
    strcpy(out, tmp);
    return DKR_FILE_OK;
}

#endif /* _WIN32 */


/* ========================================================================== *
 * Shared: recursive deletion
 * ========================================================================== *
 *
 * Written once, above the primitives, and not twice in each backend: this is
 * tree logic, not system calls. Both targets therefore run exactly the same
 * walk, which is precisely what we try to guarantee elsewhere through tests.
 *
 * The recursion is bounded by the tree's depth, and MAX_PATH bounds it in turn:
 * a path that does not fit in the buffer fails the join before the recursive
 * call.
 *
 * The directory is closed **before** descending into its entries. Keeping a
 * search open on a directory whose contents are being deleted is the kind of
 * thing Windows 95 tolerates badly, and nothing forces it: the list of names is
 * copied first.
 */
dkr_file_result dkr_file_remove_all(const char *path, unsigned long long *removed)
{
    /* One level's names, copied before descending. The bound is that of an
       ordinary working directory; beyond it we handle what we saw and then start
       again, rather than give up or grow without end. */
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
        return DKR_FILE_OK;      /* nothing to do: like std::filesystem */
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
                continue;        /* impossible to form: reported below */
            }
            memcpy(names[count], name, len + 1);
            count++;
        }
        /* If entries were left over, we will come back round. */
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
 * Shared: read-back, and its order of preference
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
    /* One separator too many is an invalid path under DOS. */
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
    /* A failure other than "absent" must not fall back to the backup copy: a
       write-protected medium or a locked file is reported as such, otherwise the
       player would believe they had lost their last session while the file is
       intact. */
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
    /* `.TMP` is never read back, even if it is there: nothing proves it is
       complete, and DKR-R's format carries no checksum that would allow checking
       it without modifying it. Losing the last write is annoying; loading a
       truncated save is no less so and is discovered much later. */
    return r;
}
