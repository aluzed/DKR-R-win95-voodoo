/* E00-S02 — Temoin T2 : T1 plus le CRT.
 * Tas, fichiers, printf, et flottant — ce dernier compte : sans SSE, tout passe
 * par la pile x87, et c'est la que les CRT recents trebuchent.
 * Ecrit son resultat sur D: parce qu'une console DOS ne se relit pas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>

int main(void)
{
    char *buf;
    FILE *f;
    double a = 3.0, b = 7.0, r;
    size_t n = 0;
    char msg[512];
    int len;

    buf = (char *)malloc(64 * 1024);
    if (!buf) { MessageBoxA(NULL, "T2: malloc a echoue", "T2", 0x10); return 1; }
    memset(buf, 0x5A, 64 * 1024);

    /* Flottant : racine, trigonometrie, conversion — le chemin x87 complet. */
    r = sqrt(a * a + b * b) + sin(a) * cos(b) + log(b) / exp(0.5);

    f = fopen("D:\\T2.TXT", "wb");
    if (!f) { free(buf); MessageBoxA(NULL, "T2: fopen a echoue", "T2", 0x10); return 2; }
    len = sprintf(msg,
                  "T2 CRT\r\n"
                  "  malloc 64 Kio  : ok (temoin 0x%02X)\r\n"
                  "  flottant x87   : %.6f\r\n"
                  "  sizeof(size_t) : %d\r\n",
                  (unsigned char)buf[65535], r, (int)sizeof(size_t));
    fwrite(msg, 1, (size_t)len, f);
    fclose(f);

    /* Relecture : prouve fread autant que fwrite. */
    f = fopen("D:\\T2.TXT", "rb");
    if (f) { n = fread(buf, 1, 128, f); fclose(f); }

    sprintf(msg, "T2 : CRT operationnel.\n\nflottant = %.4f\nrelu = %d octets", r, (int)n);
    MessageBoxA(NULL, msg, "Temoin T2", 0x40);
    free(buf);
    return 0;
}
