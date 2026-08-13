/* E07-S03 — cale de casse, comme celle de `Windows.h`.
 *
 * `game_main.cpp` ecrit `#include <DbgHelp.h>`, avec deux majuscules, ce qui est
 * l'usage de Microsoft. Le systeme de fichiers de Windows etant insensible a la
 * casse, cela fonctionne avec MSVC comme avec mingw sur Windows.
 *
 * mingw-w64 ne fournit que `dbghelp.h` en minuscules, et un systeme de fichiers
 * Linux ne les confond pas. Ce fichier n'est donc pas un contournement d'une
 * limite de Windows 95 : c'est une propriete de la machine sur laquelle on
 * compile.
 */
#include <dbghelp.h>
