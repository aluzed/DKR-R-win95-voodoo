/* E07-S03 - case shim, like the `Windows.h` one.
 *
 * `game_main.cpp` writes `#include <DbgHelp.h>`, with two capitals, which is
 * Microsoft's usage. Windows' file system being case-insensitive, that works
 * with MSVC as with mingw on Windows.
 *
 * mingw-w64 only provides `dbghelp.h` in lower case, and a Linux file system
 * does not conflate the two. This file is therefore not a workaround for a
 * Windows 95 limitation: it is a property of the machine we compile on.
 */
#include <dbghelp.h>
