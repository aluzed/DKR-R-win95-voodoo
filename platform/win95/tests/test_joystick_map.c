/* E06-S02 - the joystick mapping, on the host. Build and run:
 *   cc -Iplatform/win95 platform/win95/tests/test_joystick_map.c \
 *      platform/win95/joystick_map.c -o /tmp/tjm && /tmp/tjm
 */
#include "joystick_map.h"
#include <math.h>
#include <stdio.h>

static int failures = 0;
static void check(const char *what, int ok)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) { failures++; }
}
static int near(float a, float b) { return fabsf(a - b) < 0.001f; }

int main(void)
{
    dkr_joy_config c;
    dkr_joy_reading r = {32767, 32767, 0, -1};
    dkr_joy_n64 o;

    dkr_joy_config_default(&c, 0, 65535, 0, 65535);
    dkr_joy_map(&c, &r, &o);
    check("centred reads zero on both axes", near(o.stick_x, 0) && near(o.stick_y, 0));
    check("and no button", o.buttons == 0);

    check("the dead zone swallows 10% of drift", near(dkr_joy_axis(32767 + 3276, 0, 65535, 15), 0));
    check("full right reads 1", near(dkr_joy_axis(65535, 0, 65535, 15), 1.0f));
    check("full left reads -1", near(dkr_joy_axis(0, 0, 65535, 15), -1.0f));
    check("half right reads past the dead zone, rescaled",
          near(dkr_joy_axis(49151, 0, 65535, 15), (0.5f - 0.15f) / 0.85f));
    check("a value outside the caps is clamped", near(dkr_joy_axis(70000, 0, 65535, 0), 1.0f));
    check("an empty range reads zero", near(dkr_joy_axis(5, 10, 10, 15), 0));

    r.x = 32767; r.y = 0;       /* pushed forward */
    dkr_joy_map(&c, &r, &o);
    check("forward is stick up (Y inverted)", near(o.stick_y, 1.0f));

    r.y = 32767; r.buttons = 0x9;   /* buttons 1 and 4 */
    dkr_joy_map(&c, &r, &o);
    check("buttons 1 and 4 are A and Start", o.buttons == (DKR_N64_A | DKR_N64_START));

    r.buttons = 0; r.pov = 4500;    /* up-right diagonal */
    dkr_joy_map(&c, &r, &o);
    check("the hat's diagonal sets up and right", o.buttons == (DKR_N64_DUP | DKR_N64_DRIGHT));
    r.pov = 18000;
    dkr_joy_map(&c, &r, &o);
    check("the hat down is D-pad down", o.buttons == DKR_N64_DDOWN);
    r.pov = -1;

    check("a custom table is parsed", dkr_joy_parse_buttons(&c, "b, a ,-,start,cu") == 5);
    r.buttons = 0x3;
    dkr_joy_map(&c, &r, &o);
    check("and applied: button 1 B, 2 A", o.buttons == (DKR_N64_A | DKR_N64_B));
    r.buttons = 0x10;
    dkr_joy_map(&c, &r, &o);
    check("button 5 is C-up", o.buttons == DKR_N64_CUP);
    check("an unknown name is not counted", dkr_joy_parse_buttons(&c, "A,XYZ") == 1);

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
