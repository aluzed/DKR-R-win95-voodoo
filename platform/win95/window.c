/* E06-S01 — implementation. The contract and the reasoning live in `window.h`. */

#include "window.h"

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static HWND  g_hwnd;
static HCURSOR g_cursor_saved;
static int   g_quit;
static int   g_focused;
static unsigned char g_keys[256];
/* Pressed since the last `dkr_window_latch_clear`, whether or not still held.
   See `window.h` for why a 170 ms frame makes this necessary rather than nice. */
static unsigned char g_latch[256];

/* The class name is not the window title and does not need to be pretty; it needs
   to be unlikely to collide with anything else registered in the session. */
static const char *const kClassName = "DkrWin95Window";

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    /* --- Shutdown, by every route it can arrive ---------------------------- *
     *
     * `WM_CLOSE` is the button and `Alt+F4`; `WM_QUERYENDSESSION` is Windows
     * shutting down. Both set the flag and let `dkr_window_pump` report it, so
     * that the *game* decides when to stop rather than being torn down from
     * inside a message handler while its threads are running.
     *
     * `WM_QUERYENDSESSION` returns TRUE — refusing the session's end would leave
     * the user unable to shut their machine down because a game said no. */
    case WM_CLOSE:
        g_quit = 1;
        return 0;
    case WM_QUERYENDSESSION:
        g_quit = 1;
        return TRUE;
    case WM_ENDSESSION:
        g_quit = 1;
        return 0;
    case WM_DESTROY:
        g_quit = 1;
        PostQuitMessage(0);
        return 0;

    /* --- Focus -------------------------------------------------------------- *
     *
     * `WM_ACTIVATEAPP` and not `WM_SETFOCUS`: the first fires when the
     * *application* gains or loses the foreground, which is what matters with a
     * passthrough card — the switch that takes the screen back is a task switch,
     * not a change of focus between windows of ours.
     *
     * Losing focus clears the keys. A key held down when the window goes away
     * never receives its `WM_KEYUP`, and would stay pressed for ever: the
     * accelerator stuck on after an `Alt+Tab`, which reads as a game defect. */
    case WM_ACTIVATEAPP:
        g_focused = (wp != 0);
        if (!g_focused) { dkr_window_keys_clear(); }
        return 0;

    /* --- The keyboard ------------------------------------------------------- *
     *
     * `SYS` variants included: `Alt` is held for `Alt+F4` and Windows routes
     * every keystroke through `WM_SYSKEYDOWN` while it is down. Without them a
     * key pressed with `Alt` would be seen going down and never coming up.
     *
     * The repeat bit is not consulted: the array is a state, not an event
     * stream, and a repeat of a key already down changes nothing. */
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp < 256u) { g_keys[wp] = 1; g_latch[wp] = 1; }
        /* `Alt+F4` still has to close the window, and a handled `WM_SYSKEYDOWN`
           never reaches `DefWindowProc` to do it. */
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) { break; }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wp < 256u) { g_keys[wp] = 0; }
        return 0;

    /* --- The cursor --------------------------------------------------------- *
     *
     * Hidden over our own window. `SetCursor(NULL)` per `WM_SETCURSOR` rather
     * than `ShowCursor(FALSE)` once, because `ShowCursor` keeps a global counter
     * that a crash leaves decremented — and the player is then left on a Windows
     * 95 desktop with no pointer, which needs a reboot to fix. */
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;

    /* The window is never painted: the Voodoo owns the screen. Validating the
       region stops Windows re-posting `WM_PAINT` for ever, which would spin this
       loop at whatever rate the pump runs. */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    /* Windows plays a system sound when a key reaches an unhandled
       `WM_SYSCHAR` — `Alt+`anything, which the player will hit. */
    case WM_SYSCHAR:
        return 0;

    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int dkr_window_open(const char *title)
{
    WNDCLASSA wc;
    HINSTANCE inst;

    if (g_hwnd != NULL) { return 1; }

    inst = GetModuleHandleA(NULL);
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = kClassName;
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    /* No background brush: painting a background would flash white over the
       desktop on every uncovering, and nothing is ever drawn here anyway. */
    wc.hbrBackground = NULL;

    if (RegisterClassA(&wc) == 0) {
        /* A class already registered is not a failure — a previous run in the
           same process, or a second call — and any other error is. */
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            fprintf(stderr, "[boot][window] RegisterClassA failed: %lu\n",
                    (unsigned long)GetLastError());
            return 0;
        }
    }

    /* **A small window, and a real one.** It is tempting to create it hidden or
       zero-sized since nothing is drawn in it, and both are traps: Windows 95
       does not give the foreground to a hidden window, and a window with no
       surface is skipped by the task switcher — so the player would have no way
       to give the game the keyboard back after switching away from it. */
    g_hwnd = CreateWindowExA(
        0, kClassName, (title != NULL) ? title : "DKR-R",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 200,
        NULL, NULL, inst, NULL);
    if (g_hwnd == NULL) {
        fprintf(stderr, "[boot][window] CreateWindowExA failed: %lu\n",
                (unsigned long)GetLastError());
        return 0;
    }

    g_cursor_saved = LoadCursorA(NULL, IDC_ARROW);
    g_quit = 0;
    dkr_window_keys_clear();

    /* Shown and given the foreground **before** Glide is opened on it. Once the
       card holds the screen, asking Windows to raise a window is asking it to
       take the display back -- which is exactly what happened when this call sat
       after `grSstWinOpen`. */
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    g_focused = 1;

    fprintf(stderr, "[boot][window] created, foreground, keyboard live\n");
    return 1;
}

int dkr_window_pump(void)
{
    MSG msg;
    if (g_hwnd == NULL) { return 1; }
    /* `PeekMessage` with `PM_REMOVE`, in a loop until the queue is empty. A
       single peek per call would let the queue grow faster than it is drained
       when the game thread is slow -- and at 170 ms a frame (E00-S03) it is
       slow. The keyboard would then lag by however far behind the queue had
       fallen. */
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { g_quit = 1; break; }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return g_quit ? 0 : 1;
}

int dkr_window_focused(void) { return g_focused; }

unsigned long dkr_window_handle(void) { return (unsigned long)(UINT_PTR)g_hwnd; }

int dkr_window_key_down(int vk)
{
    if (vk < 0 || vk > 255) { return 0; }
    return (g_keys[vk] != 0) || (g_latch[vk] != 0);
}

void dkr_window_latch_clear(void)
{
    memset(g_latch, 0, sizeof(g_latch));
}

void dkr_window_keys_clear(void)
{
    memset(g_keys, 0, sizeof(g_keys));
    memset(g_latch, 0, sizeof(g_latch));
}

void dkr_window_close(void)
{
    if (g_hwnd == NULL) { return; }
    /* The cursor is restored before the window goes: after `DestroyWindow` there
       is no client area left to own it, and a pointer left hidden is a reboot on
       this machine. */
    SetCursor(g_cursor_saved);
    DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    g_focused = 0;
    dkr_window_keys_clear();
}

#else /* not _WIN32 — the host build, so that the seam compiles everywhere */

int  dkr_window_open(const char *title) { (void)title; return 1; }
int  dkr_window_pump(void)   { return 1; }
int  dkr_window_focused(void) { return 1; }
unsigned long dkr_window_handle(void) { return 0; }
int  dkr_window_key_down(int vk) { (void)vk; return 0; }
void dkr_window_keys_clear(void) { }
void dkr_window_latch_clear(void) { }
void dkr_window_close(void)  { }

#endif /* _WIN32 */
