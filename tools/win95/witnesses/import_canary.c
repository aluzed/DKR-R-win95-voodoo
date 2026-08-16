/* E01-S04 - test source for the import check.
 *
 * Compiled only when `DKR_WIN95_SELFTEST_IMPORT` is active. It imports an API
 * Windows 95 does not export: the post-link check must fail the build while
 * naming it, and naming this object.
 *
 * The function is declared here rather than included from <windows.h>: the
 * toolchain sets `_WIN32_WINNT=0x0400`, which hides it precisely - and that is
 * the other guard rail, the compile-time one. Deliberately circumventing it is
 * the only way to test the link-time one.
 *
 * The choice of symbol is not incidental. The first version of this canary
 * imported `GetTickCount64` and the build passed: `win95compat` supplies it, so
 * the linker resolved the import to the bridge rather than to KERNEL32. The test
 * failed to fail - which was, in itself, the demonstration that the bridge does
 * intercept. So a symbol the bridge does not cover is needed.
 */
typedef struct { void *p; } DKR_FAKE_CONDITION_VARIABLE;

__declspec(dllimport) void __stdcall
InitializeConditionVariable(DKR_FAKE_CONDITION_VARIABLE *cv);

void dkr_import_canary(DKR_FAKE_CONDITION_VARIABLE *cv)
{
    InitializeConditionVariable(cv);
}
