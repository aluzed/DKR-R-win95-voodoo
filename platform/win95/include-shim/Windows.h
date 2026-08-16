/* E01-S02 - case shim for cross-compiling from Linux.
 *
 * `ultramodern` writes `#include <Windows.h>` with a capital, which is
 * Microsoft's usage. Windows' file system being case-insensitive, that works
 * with MSVC as with mingw on Windows.
 *
 * mingw-w64 only provides `windows.h` in lower case, and a Linux file system
 * does not conflate the two. This file is therefore not a workaround for a
 * Windows 95 limitation: it is a property of the machine we compile on, and it
 * has no place in a dependency patch.
 */
#include <windows.h>
