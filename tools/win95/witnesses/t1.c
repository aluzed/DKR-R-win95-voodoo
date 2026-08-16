/* E00-S02 - Witness T1: PE format, subsystem, basic imports.
 * No CRT, no library: if this does not start, nothing will. */
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
    MessageBoxA(NULL, "T1: PE32 starts under Windows 95.", "Witness T1", 0x40);
    return 0;
}
#else
void start(void)
{
    MessageBoxA(NULL, "T1: PE32 starts under Windows 95.", "Witness T1", 0x40);
    ExitProcess(0);
}
#endif
