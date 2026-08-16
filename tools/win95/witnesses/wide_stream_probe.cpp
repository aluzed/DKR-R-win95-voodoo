/* E02-S05 - `std::ofstream` constructed on a `std::filesystem::path`.
 *
 * Under MinGW, `path::value_type` is `wchar_t`: handing a `path` to a stream
 * makes it open through `_wfopen`, hence through the wide API. Under Windows 9x
 * that family is a stub - it loads and does nothing.
 *
 * The question is not theoretical: `save_manager` fails on the machine with
 * "Could not create the temporary save file", and this is the likely
 * explanation. This probe turns it into a measurement, by opening the same file
 * four ways.
 */
#include <cstdio>
#include <filesystem>
#include <fstream>

static FILE *g_log;
static void say(const char *what, bool ok)
{
    std::printf("  %-34s : %s\n", what, ok ? "OK" : "FAILED");
    if (g_log) std::fprintf(g_log, "  %-34s : %s\n", what, ok ? "OK" : "FAILED");
}

int main()
{
    g_log = std::fopen("D:\\WPROBE.TXT", "w");
    const std::filesystem::path p{"D:\\WPROBE.DAT"};

    { std::ofstream f(p, std::ios::binary);
      say("ofstream(path)", (bool)f); }
    { std::ofstream f(p.string(), std::ios::binary);
      say("ofstream(path.string())", (bool)f); }
    { std::ofstream f("D:\\WPROBE.DAT", std::ios::binary);
      f << "abc";
      say("ofstream(narrow literal)", (bool)f); }
    { FILE *f = std::fopen("D:\\WPROBE.DAT", "rb");
      say("narrow fopen", f != nullptr); if (f) std::fclose(f); }
    { std::ifstream f(p, std::ios::binary);
      say("ifstream(path)", (bool)f); }
    { std::ifstream f(p.string(), std::ios::binary);
      say("ifstream(path.string())", (bool)f); }

    if (g_log) std::fclose(g_log);
    return 0;
}
