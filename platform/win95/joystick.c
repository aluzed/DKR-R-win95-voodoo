/* E06-S02 - implementation. See joystick.h. */
#include "joystick.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int           present;
    int           has_pov;
    DWORD         next_probe;   /* GetTickCount when absent */
    dkr_joy_config config;
} joy_device;

static joy_device g_devices[DKR_JOY_DEVICES];
static int g_disabled = -1;

static void configure(int index)
{
    joy_device *d = &g_devices[index];
    JOYCAPSA caps;
    const char *dead = getenv("DKR_JOY_DEADZONE");
    const char *buttons = getenv("DKR_JOY_BUTTONS");
    memset(&caps, 0, sizeof(caps));
    d->present = 0;
    if (joyGetDevCapsA((UINT)(JOYSTICKID1 + index), &caps, sizeof(caps)) != JOYERR_NOERROR) {
        return;
    }
    dkr_joy_config_default(&d->config, caps.wXmin, caps.wXmax, caps.wYmin, caps.wYmax);
    if (dead) {
        const long percent = strtol(dead, NULL, 10);
        if (percent >= 0 && percent <= 90) {
            d->config.dead_zone_percent = (int)percent;
        } else {
            fprintf(stderr, "[input] JOY_DEADZONE=%s is outside 0..90: kept at 15\n", dead);
        }
    }
    if (buttons && dkr_joy_parse_buttons(&d->config, buttons) == 0) {
        fprintf(stderr, "[input] JOY_BUTTONS=%s names no N64 button: the default kept\n",
                buttons);
        dkr_joy_config_default(&d->config, caps.wXmin, caps.wXmax, caps.wYmin, caps.wYmax);
    }
    d->has_pov = (caps.wCaps & JOYCAPS_HASPOV) != 0;
    {
        JOYINFOEX info;
        memset(&info, 0, sizeof(info));
        info.dwSize = sizeof(info);
        info.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx((UINT)(JOYSTICKID1 + index), &info) != JOYERR_NOERROR) {
            return;       /* configured in the control panel, not plugged in */
        }
    }
    d->present = 1;
    fprintf(stderr, "[input] joystick %d: \"%s\", %u buttons, x %u..%u, y %u..%u%s, "
                    "dead zone %d%% (player %d)\n",
            index + 1, caps.szPname, caps.wNumButtons, caps.wXmin, caps.wXmax,
            caps.wYmin, caps.wYmax, d->has_pov ? ", hat" : "",
            d->config.dead_zone_percent, index + 1);
}

int dkr_joy_read(int index, dkr_joy_n64 *out)
{
    joy_device *d;
    JOYINFOEX info;
    dkr_joy_reading r;
    out->stick_x = 0.0f; out->stick_y = 0.0f; out->buttons = 0;
    if (index < 0 || index >= DKR_JOY_DEVICES) { return 0; }
    if (g_disabled < 0) {
        const char *joy = getenv("DKR_JOY");
        g_disabled = (joy != NULL && (strcmp(joy, "off") == 0 || strcmp(joy, "0") == 0));
        if (g_disabled) { fprintf(stderr, "[input] joysticks off (JOY=%s)\n", joy); }
    }
    if (g_disabled) { return 0; }
    d = &g_devices[index];
    if (!d->present) {
        /* Probed at most every three seconds: a missing device costs a driver
           call each time, and a pad plugged in later is still found. */
        const DWORD now = GetTickCount();
        if ((LONG)(now - d->next_probe) < 0) { return 0; }
        d->next_probe = now + 3000u;
        configure(index);
        if (!d->present) { return 0; }
    }
    memset(&info, 0, sizeof(info));
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS |
                   (d->has_pov ? JOY_RETURNPOV : 0u);
    if (joyGetPosEx((UINT)(JOYSTICKID1 + index), &info) != JOYERR_NOERROR) {
        fprintf(stderr, "[input] joystick %d stopped answering\n", index + 1);
        d->present = 0;
        d->next_probe = GetTickCount() + 3000u;
        return 0;
    }
    r.x = info.dwXpos;
    r.y = info.dwYpos;
    r.buttons = info.dwButtons;
    r.pov = (d->has_pov && info.dwPOV != JOY_POVCENTERED) ? (int)info.dwPOV : -1;
    dkr_joy_map(&d->config, &r, out);
    return 1;
}
