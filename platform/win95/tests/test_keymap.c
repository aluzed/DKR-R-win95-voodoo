/* E06-S02 - player one's keyboard map (`keymap.c`), on the host. */
#include "keymap.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void check(const char *what, int ok)
{
    printf("  %s  %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) { g_failures++; }
}

/* The keys held for a read: positions, 0-terminated. */
static const int *g_held;
static int down(int position)
{
    const int *p;
    for (p = g_held; *p != 0; p++) { if (*p == position) { return 1; } }
    return 0;
}

static dkr_keymap_state read_with(const dkr_keymap *map, const int *held)
{
    dkr_keymap_state s;
    g_held = held;
    dkr_keymap_read(map, down, &s);
    return s;
}

int main(void)
{
    dkr_keymap map;
    dkr_keymap_state s;
    int refused = -1;

    printf("keyboard map\n");
    dkr_keymap_default(&map);

    {   /* W and D: stick up and right, no button. */
        const int held[] = {0x11, 0x20, 0};
        s = read_with(&map, held);
        check("W and D push the stick up and right, by position",
              s.stick_x == 1.0f && s.stick_y == 1.0f && s.buttons == 0);
    }
    {   /* A and D together cancel out. */
        const int held[] = {0x1E, 0x20, 0};
        s = read_with(&map, held);
        check("A and D together leave the stick centred", s.stick_x == 0.0f);
    }
    {   /* Space, right Shift, Enter: A, B, Start. */
        const int held[] = {0x39, 0x36, 0x1C, 0};
        s = read_with(&map, held);
        check("Space, either Shift and Enter are A, B and Start",
              s.buttons == (0x8000u | 0x4000u | 0x1000u));
    }
    {   /* The up arrow is extended; keypad 8 shares its scan code and is not. */
        const int arrow[] = {0x80 | 0x48, 0};
        const int keypad8[] = {0x48, 0};
        s = read_with(&map, arrow);
        check("the up arrow is the D-pad's up", s.buttons == 0x0800u);
        s = read_with(&map, keypad8);
        check("keypad 8, same scan code without the extended bit, is not",
              s.buttons == 0 && s.stick_y == 0.0f);
    }
    {   /* The C buttons and the shoulders. */
        const int held[] = {0x17, 0x25, 0x24, 0x26, 0x10, 0x12, 0};
        s = read_with(&map, held);
        check("I K J L are the C buttons, Q and E are L and R",
              s.buttons == (0x0008u | 0x0004u | 0x0002u | 0x0001u | 0x0020u | 0x0010u));
    }

    {   /* A remap: the stick on the arrows, A on X; the rest kept. */
        const int applied = dkr_keymap_parse(&map,
            "SL:LEFT, SR:RIGHT,su:up,SD:DOWN,A:X", &refused);
        check("five entries applied, none refused, case and spaces ignored",
              applied == 5 && refused == 0);
        {
            const int held[] = {0x80 | 0x4B, 0x2D, 0};
            s = read_with(&map, held);
            /* The D-pad keeps the arrows too: a key may serve two controls. */
            check("after it, the left arrow is the stick's left (and still the D-pad's), X is A",
                  s.stick_x == -1.0f && s.buttons == (0x8000u | 0x0200u));
        }
        {
            const int held[] = {0x39, 0x2C, 0};
            s = read_with(&map, held);
            check("Space no longer A; Z, not named, still Z",
                  s.buttons == 0x2000u);
        }
    }
    {   /* Two keys for one control. */
        dkr_keymap_default(&map);
        check("B:C/V applied", dkr_keymap_parse(&map, "B:C/V", &refused) == 1 && refused == 0);
        {
            const int held[] = {0x2F, 0};
            s = read_with(&map, held);
            check("V, the second key, is B", s.buttons == 0x4000u);
        }
    }
    {   /* Refusals change nothing. */
        dkr_keymap_default(&map);
        check("an unknown control, an unknown key, an entry without a colon: refused",
              dkr_keymap_parse(&map, "JUMP:SPACE,A:F13,START", &refused) == 0 && refused == 3);
        {
            const int held[] = {0x39, 0};
            s = read_with(&map, held);
            check("and the defaults are untouched", s.buttons == 0x8000u);
        }
    }
    check("key names: unknown is 0, case does not matter",
          dkr_keymap_key("nope", 4) == 0 && dkr_keymap_key("space", 5) == 0x39);

    printf("\n%d failure(s)\n", g_failures);
    return g_failures != 0;
}
