/* E00-S02 — Temoin T1 : format PE, sous-systeme, imports de base.
 * Aucun CRT, aucune bibliotheque : si ceci ne demarre pas, rien ne demarrera. */
#ifdef __WATCOMC__
#  include <windows.h>
#else
#  define WINAPI __stdcall
__declspec(dllimport) int  WINAPI MessageBoxA(void *, const char *, const char *, unsigned);
__declspec(dllimport) void WINAPI ExitProcess(unsigned);
#  define NULL ((void *)0)
#endif

#ifdef __WATCOMC__
int main(void)
{
    MessageBoxA(NULL, "T1 : PE32 demarre sous Windows 95.", "Temoin T1", 0x40);
    return 0;
}
#else
void start(void)
{
    MessageBoxA(NULL, "T1 : PE32 demarre sous Windows 95.", "Temoin T1", 0x40);
    ExitProcess(0);
}
#endif
