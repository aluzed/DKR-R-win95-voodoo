/* E01-S02 — cale de casse pour la compilation croisee depuis Linux.
 *
 * `ultramodern` ecrit `#include <Windows.h>` avec une majuscule, ce qui est
 * l'usage de Microsoft. Le systeme de fichiers de Windows etant insensible a la
 * casse, cela fonctionne avec MSVC comme avec mingw sur Windows.
 *
 * mingw-w64 ne fournit que `windows.h` en minuscules, et un systeme de fichiers
 * Linux ne les confond pas. Ce fichier n'est donc pas un contournement d'une
 * limite de Windows 95 : c'est une propriete de la machine sur laquelle on
 * compile, et il n'a pas sa place dans un patch de dependance.
 */
#include <windows.h>
