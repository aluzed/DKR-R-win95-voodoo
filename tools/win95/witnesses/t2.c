/* E00-S02 - Witness T2: T1 plus the CRT.
 * Heap, files, printf, and floating point - the last one counts: without SSE
 * everything goes through the x87 stack, and that is where recent CRTs stumble.
 * Writes its result to D: because a DOS console cannot be read back. */
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
    if (!buf) { MessageBoxA(NULL, "T2: malloc failed", "T2", 0x10); return 1; }
    memset(buf, 0x5A, 64 * 1024);

    /* Floating point: root, trigonometry, conversion - the full x87 path. */
    r = sqrt(a * a + b * b) + sin(a) * cos(b) + log(b) / exp(0.5);

    f = fopen("D:\\T2.TXT", "wb");
    if (!f) { free(buf); MessageBoxA(NULL, "T2: fopen failed", "T2", 0x10); return 2; }
    len = sprintf(msg,
                  "T2 CRT\r\n"
                  "  malloc 64 KiB  : ok (witness 0x%02X)\r\n"
                  "  x87 float      : %.6f\r\n"
                  "  sizeof(size_t) : %d\r\n",
                  (unsigned char)buf[65535], r, (int)sizeof(size_t));
    fwrite(msg, 1, (size_t)len, f);
    fclose(f);

    /* Read back: proves fread as much as fwrite. */
    f = fopen("D:\\T2.TXT", "rb");
    if (f) { n = fread(buf, 1, 128, f); fclose(f); }

    sprintf(msg, "T2: CRT working.\n\nfloat = %.4f\nread back = %d bytes", r, (int)n);
    MessageBoxA(NULL, msg, "Witness T2", 0x40);
    free(buf);
    return 0;
}
