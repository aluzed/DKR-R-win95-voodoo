/* E06-S02 - from a Windows 95 joystick's reading to an N64 controller's.
 *
 * Pure arithmetic, so that the host can test it: `joystick.c` reads the device
 * through winmm and hands the numbers here.
 *
 * - **Axes.** `joyGetPosEx` returns positions already corrected by the system's
 *   own calibration (Control Panel > Joystick), inside the range
 *   `joyGetDevCaps` reports. Each axis is scaled to -1..1 around the middle of
 *   that range; Y is inverted, since a joystick pushed forward reads low and the
 *   N64 stick reads up as positive.
 * - **Dead zone.** A share of the half-range, 0..90 percent, under which the
 *   axis reads zero; the rest is rescaled so that the stick still reaches full
 *   deflection. Pads of the period drift at rest; the default is 15%.
 * - **Buttons.** Up to eight, each assigned an N64 button by a table the caller
 *   may replace (`DKR_JOY_BUTTONS`). The default suits a four-button pad: 1 A,
 *   2 B, 3 Z, 4 Start, then L, R, C-down, C-left.
 * - **The hat** (point of view), when there is one, is the D-pad.
 */
#ifndef DKR_WIN95_JOYSTICK_MAP_H
#define DKR_WIN95_JOYSTICK_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* N64 controller button bits, as `runtime_platform.cpp` stores them. */
#define DKR_N64_A       0x8000u
#define DKR_N64_B       0x4000u
#define DKR_N64_Z       0x2000u
#define DKR_N64_START   0x1000u
#define DKR_N64_DUP     0x0800u
#define DKR_N64_DDOWN   0x0400u
#define DKR_N64_DLEFT   0x0200u
#define DKR_N64_DRIGHT  0x0100u
#define DKR_N64_L       0x0020u
#define DKR_N64_R       0x0010u
#define DKR_N64_CUP     0x0008u
#define DKR_N64_CDOWN   0x0004u
#define DKR_N64_CLEFT   0x0002u
#define DKR_N64_CRIGHT  0x0001u

#define DKR_JOY_MAX_BUTTONS 8

typedef struct {
    unsigned x_min, x_max, y_min, y_max;   /* from joyGetDevCaps */
    int dead_zone_percent;                 /* 0..90 */
    unsigned short button_map[DKR_JOY_MAX_BUTTONS];
} dkr_joy_config;

typedef struct {
    unsigned x, y;          /* raw positions, inside the caps' range */
    unsigned buttons;       /* bit n = button n+1 */
    int pov;                /* hundredths of a degree, or -1 when centred / absent */
} dkr_joy_reading;

typedef struct {
    float stick_x, stick_y; /* -1..1, up and right positive */
    unsigned short buttons; /* N64 bits */
} dkr_joy_n64;

/* The default configuration for a range and a dead zone. */
void dkr_joy_config_default(dkr_joy_config *config, unsigned x_min, unsigned x_max,
                            unsigned y_min, unsigned y_max);

/* Replaces the button table from a comma-separated list of N64 button names
   (A B Z START L R CU CD CL CR DU DD DL DR, or "-" for none), in the order of
   the joystick's buttons. Returns the number of names understood; an unknown
   name leaves that button unassigned and is not counted. */
int dkr_joy_parse_buttons(dkr_joy_config *config, const char *list);

/* One axis, scaled and dead-zoned. Exposed for the tests. */
float dkr_joy_axis(unsigned value, unsigned min, unsigned max, int dead_zone_percent);

/* A whole reading. */
void dkr_joy_map(const dkr_joy_config *config, const dkr_joy_reading *reading,
                 dkr_joy_n64 *out);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_JOYSTICK_MAP_H */
