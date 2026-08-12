/* E01-S04 — source d'epreuve du controle des imports.
 *
 * Compilee uniquement quand `DKR_WIN95_SELFTEST_IMPORT` est actif. Elle importe
 * une API que Windows 95 n'exporte pas : le controle post-lien doit faire
 * echouer le build en la nommant, et en nommant cet objet.
 *
 * La fonction est declaree ici plutot qu'incluse depuis <windows.h> : la
 * toolchain pose `_WIN32_WINNT=0x0400`, qui la masque justement — et c'est
 * l'autre garde-fou, celui de la compilation. Le contourner volontairement est
 * le seul moyen d'eprouver celui du lien.
 *
 * Le choix du symbole n'est pas indifferent. La premiere version de ce canari
 * importait `GetTickCount64` et le build passait : `win95compat` la fournit, et
 * le lieur resolvait donc l'import vers le pont plutot que vers KERNEL32.
 * L'epreuve echouait a echouer — ce qui etait, en soi, la demonstration que le
 * pont intercepte bien. Il faut donc un symbole que le pont ne couvre pas.
 */
typedef struct { void *p; } DKR_FAKE_CONDITION_VARIABLE;

__declspec(dllimport) void __stdcall
InitializeConditionVariable(DKR_FAKE_CONDITION_VARIABLE *cv);

void dkr_import_canary(DKR_FAKE_CONDITION_VARIABLE *cv)
{
    InitializeConditionVariable(cv);
}
