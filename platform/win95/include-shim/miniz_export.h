/* E01-S05 - shim for `miniz`, which expects a CMake-generated header.
 *
 * `miniz.h` includes `miniz_export.h`, which miniz's CMake produces at
 * configure time to decide the export attributes of a shared library. The
 * Windows 95 target does not build miniz through its CMake: it compiles
 * `librecomp` directly, and everything there is static (ADR 0001).
 *
 * Both macros are therefore empty, which is exactly what miniz's CMake produces
 * for a static library. This file is not a workaround for a Windows 95
 * limitation - it is a missing piece of the build path, just like the `Windows.h`
 * case shim.
 */
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

#endif /* MINIZ_EXPORT_H */
