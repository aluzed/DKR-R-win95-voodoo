/* E06-S02 - player one's keyboard: which key is which N64 control.
 *
 * **Keys by position, not by letter.** A key is named after where it sits on a
 * US keyboard and read by its scan code, the window's messages keeping one flag
 * per position (`dkr_window_position_down`). Read by letter instead -- the
 * virtual key, as this port first did -- the stick was W A S D on a US keyboard
 * and four scattered keys on the French one the test machine has; by position
 * it is the same block on both, labelled Z Q S D on an AZERTY keyboard.
 *
 * **Remappable.** `DKR_KEYS` (DKRR.INI's `KEYS=`) names controls and their keys,
 * comma-separated, `control:key`, with up to two keys per control joined by `/`:
 *
 *     KEYS=A:X,B:C,SL:LEFT,SR:RIGHT,SU:UP,SD:DOWN
 *
 * Controls: A B Z START L R, the C buttons CU CD CL CR, the D-pad DU DD DL DR,
 * and the analogue stick's four directions SU SD SL SR. A control not named keeps
 * its default; an entry the parser does not understand is skipped and counted.
 *
 * Pure table work, so that the host can test it; `runtime_platform.cpp` hands it
 * the window's key state.
 */
#ifndef DKR_WIN95_KEYMAP_H
#define DKR_WIN95_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* A key position: the set 1 scan code, plus 0x80 for the extended keys (the
   arrows, the right Ctrl, Insert and the block around it), which share their
   scan codes with the numeric keypad's. */
typedef unsigned char dkr_key_position;

/* One per control: the 14 buttons in the N64 bit order of `joystick_map.h`,
   then the stick's four directions. */
enum {
    DKR_KEYMAP_CONTROLS = 18,
    DKR_KEYMAP_KEYS_PER_CONTROL = 2
};

typedef struct {
    /* 0 is "no key": scan code 0 is not a key. */
    dkr_key_position keys[DKR_KEYMAP_CONTROLS][DKR_KEYMAP_KEYS_PER_CONTROL];
} dkr_keymap;

typedef struct {
    unsigned short buttons;   /* N64 bits */
    float stick_x, stick_y;   /* -1, 0 or 1; up and right positive */
} dkr_keymap_state;

/* The default layout: the stick on W A S D, A on Space, B on either Shift, Z on
   Z, Start on Enter, the D-pad on the arrows, L and R on Q and E, the C buttons
   on I J K L -- all by position. */
void dkr_keymap_default(dkr_keymap *map);

/* Applies a `KEYS=` list over `map`. Returns the number of entries applied;
   `*refused`, when given, receives the number skipped. */
int dkr_keymap_parse(dkr_keymap *map, const char *text, int *refused);

/* The position a key name stands for, 0 if unknown: letters, digits, SPACE,
   ENTER, TAB, BACKSPACE, LSHIFT, RSHIFT, LCTRL, RCTRL, UP, DOWN, LEFT, RIGHT,
   INSERT, DELETE, HOME, END, PGUP, PGDN, COMMA, PERIOD, SLASH, SEMICOLON,
   QUOTE, MINUS, EQUALS, LBRACKET, RBRACKET, BACKSLASH. */
dkr_key_position dkr_keymap_key(const char *name, unsigned length);

/* Reads the controls from a key state: `down(position)` non-zero while the key
   at that position is held. */
void dkr_keymap_read(const dkr_keymap *map, int (*down)(int position),
                     dkr_keymap_state *state);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_KEYMAP_H */
