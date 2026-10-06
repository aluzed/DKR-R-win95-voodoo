/* E06-S02 - player one's keyboard. See `keymap.h`. */
#include "keymap.h"

#include <string.h>

/* The controls, in table order: the buttons with their N64 bits, then the
   stick's directions (bit 0). */
static const struct { const char *name; unsigned short bit; } kControls[DKR_KEYMAP_CONTROLS] = {
    {"A", 0x8000u}, {"B", 0x4000u}, {"Z", 0x2000u}, {"START", 0x1000u},
    {"DU", 0x0800u}, {"DD", 0x0400u}, {"DL", 0x0200u}, {"DR", 0x0100u},
    {"L", 0x0020u}, {"R", 0x0010u},
    {"CU", 0x0008u}, {"CD", 0x0004u}, {"CL", 0x0002u}, {"CR", 0x0001u},
    {"SU", 0}, {"SD", 0}, {"SL", 0}, {"SR", 0},
};
enum { SU = 14, SD = 15, SL = 16, SR = 17 };

#define EXT 0x80u

/* Set 1 scan codes, by the key's place on a US keyboard. */
static const struct { const char *name; dkr_key_position position; } kKeys[] = {
    {"Q", 0x10}, {"W", 0x11}, {"E", 0x12}, {"R", 0x13}, {"T", 0x14},
    {"Y", 0x15}, {"U", 0x16}, {"I", 0x17}, {"O", 0x18}, {"P", 0x19},
    {"A", 0x1E}, {"S", 0x1F}, {"D", 0x20}, {"F", 0x21}, {"G", 0x22},
    {"H", 0x23}, {"J", 0x24}, {"K", 0x25}, {"L", 0x26},
    {"Z", 0x2C}, {"X", 0x2D}, {"C", 0x2E}, {"V", 0x2F}, {"B", 0x30},
    {"N", 0x31}, {"M", 0x32},
    {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06},
    {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B},
    {"MINUS", 0x0C}, {"EQUALS", 0x0D}, {"BACKSPACE", 0x0E}, {"TAB", 0x0F},
    {"LBRACKET", 0x1A}, {"RBRACKET", 0x1B}, {"ENTER", 0x1C},
    {"LCTRL", 0x1D}, {"SEMICOLON", 0x27}, {"QUOTE", 0x28},
    {"LSHIFT", 0x2A}, {"BACKSLASH", 0x2B}, {"COMMA", 0x33}, {"PERIOD", 0x34},
    {"SLASH", 0x35}, {"RSHIFT", 0x36}, {"SPACE", 0x39},
    {"RCTRL", EXT | 0x1D},
    {"HOME", EXT | 0x47}, {"UP", EXT | 0x48}, {"PGUP", EXT | 0x49},
    {"LEFT", EXT | 0x4B}, {"RIGHT", EXT | 0x4D},
    {"END", EXT | 0x4F}, {"DOWN", EXT | 0x50}, {"PGDN", EXT | 0x51},
    {"INSERT", EXT | 0x52}, {"DELETE", EXT | 0x53},
};

static int same(const char *a, unsigned length, const char *name)
{
    unsigned i;
    for (i = 0; i < length; i++) {
        char c = a[i];
        if (c >= 'a' && c <= 'z') { c = (char)(c - 'a' + 'A'); }
        if (name[i] == '\0' || c != name[i]) { return 0; }
    }
    return name[length] == '\0';
}

dkr_key_position dkr_keymap_key(const char *name, unsigned length)
{
    unsigned i;
    for (i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
        if (same(name, length, kKeys[i].name)) { return kKeys[i].position; }
    }
    return 0;
}

static void set(dkr_keymap *map, int control, const char *first, const char *second)
{
    map->keys[control][0] = dkr_keymap_key(first, (unsigned)strlen(first));
    map->keys[control][1] = second ? dkr_keymap_key(second, (unsigned)strlen(second)) : 0;
}

void dkr_keymap_default(dkr_keymap *map)
{
    memset(map, 0, sizeof(*map));
    set(map, 0, "SPACE", NULL);              /* A */
    set(map, 1, "LSHIFT", "RSHIFT");         /* B */
    set(map, 2, "Z", NULL);
    set(map, 3, "ENTER", NULL);              /* Start */
    set(map, 4, "UP", NULL);    set(map, 5, "DOWN", NULL);
    set(map, 6, "LEFT", NULL);  set(map, 7, "RIGHT", NULL);
    set(map, 8, "Q", NULL);     set(map, 9, "E", NULL);
    set(map, 10, "I", NULL);    set(map, 11, "K", NULL);
    set(map, 12, "J", NULL);    set(map, 13, "L", NULL);
    set(map, SU, "W", NULL);    set(map, SD, "S", NULL);
    set(map, SL, "A", NULL);    set(map, SR, "D", NULL);
}

/* One `control:key[/key]` entry, `length` characters long. */
static int apply(dkr_keymap *map, const char *entry, unsigned length)
{
    const char *colon;
    const char *slash;
    unsigned control_length, first_length;
    int control;
    dkr_key_position first, second = 0;
    while (length > 0 && (*entry == ' ' || *entry == '\t')) { entry++; length--; }
    while (length > 0 && (entry[length - 1] == ' ' || entry[length - 1] == '\t')) { length--; }
    colon = memchr(entry, ':', length);
    if (!colon) { return 0; }
    control_length = (unsigned)(colon - entry);
    for (control = 0; control < DKR_KEYMAP_CONTROLS; control++) {
        if (same(entry, control_length, kControls[control].name)) { break; }
    }
    if (control == DKR_KEYMAP_CONTROLS) { return 0; }
    entry = colon + 1;
    length -= control_length + 1;
    slash = memchr(entry, '/', length);
    first_length = slash ? (unsigned)(slash - entry) : length;
    first = dkr_keymap_key(entry, first_length);
    if (first == 0) { return 0; }
    if (slash) {
        second = dkr_keymap_key(slash + 1, length - first_length - 1);
        if (second == 0) { return 0; }
    }
    map->keys[control][0] = first;
    map->keys[control][1] = second;
    return 1;
}

int dkr_keymap_parse(dkr_keymap *map, const char *text, int *refused)
{
    int applied = 0, skipped = 0;
    while (text && *text) {
        const char *comma = strchr(text, ',');
        const unsigned length = comma ? (unsigned)(comma - text) : (unsigned)strlen(text);
        if (length > 0) {
            if (apply(map, text, length)) { applied++; } else { skipped++; }
        }
        text = comma ? comma + 1 : NULL;
    }
    if (refused) { *refused = skipped; }
    return applied;
}

static int held(const dkr_keymap *map, int control, int (*down)(int))
{
    int k;
    for (k = 0; k < DKR_KEYMAP_KEYS_PER_CONTROL; k++) {
        const dkr_key_position position = map->keys[control][k];
        if (position != 0 && down(position)) { return 1; }
    }
    return 0;
}

void dkr_keymap_read(const dkr_keymap *map, int (*down)(int position),
                     dkr_keymap_state *state)
{
    int control;
    state->buttons = 0;
    for (control = 0; control < SU; control++) {
        if (held(map, control, down)) { state->buttons |= kControls[control].bit; }
    }
    state->stick_x = (float)(held(map, SR, down) - held(map, SL, down));
    state->stick_y = (float)(held(map, SU, down) - held(map, SD, down));
}
