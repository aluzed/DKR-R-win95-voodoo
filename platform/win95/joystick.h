/* E06-S02 - joysticks and gamepads through winmm, on Windows 95.
 *
 * `joyGetPosEx` is Windows 95's own joystick interface, present on every
 * installation and fed by whatever driver the pad came with; DirectInput is
 * the other route and is not taken here (see the ticket). Up to two devices,
 * JOYSTICKID1 and JOYSTICKID2 -- the game port of the period carries two -- for
 * players one and two. The system's calibration (Control Panel > Joystick) is
 * already applied to what `joyGetPosEx` returns.
 *
 * Settings (DKRR.INI's [Settings], or the environment):
 *   JOY_DEADZONE=<0..90>        percent of the half-range, default 15
 *   JOY_BUTTONS=A,B,Z,START,... N64 buttons for the pad's buttons 1..8
 *   JOY=off                     ignore joysticks entirely
 */
#ifndef DKR_WIN95_JOYSTICK_H
#define DKR_WIN95_JOYSTICK_H

#include "joystick_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DKR_JOY_DEVICES 2

/* Reads device `index` (0 or 1) into `out`. Returns 1 when a device is there
   and answered, 0 otherwise; `out` is then left neutral. Detection is retried
   every few seconds for a device that is absent, so a pad plugged in after the
   start is found. */
int dkr_joy_read(int index, dkr_joy_n64 *out);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_JOYSTICK_H */
