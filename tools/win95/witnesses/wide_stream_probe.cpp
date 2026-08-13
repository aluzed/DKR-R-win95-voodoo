/* E02-S05 — `std::ofstream` construit sur un `std::filesystem::path`.
 *
 * Sous MinGW, `path::value_type` est `wchar_t` : passer un `path` a un flux le
 * fait ouvrir par `_wfopen`, donc par l'API large. Sous Windows 9x cette
 * famille est un bouchon — elle se charge et ne fait rien.
 *
 * La question n'est pas theorique : `save_manager` echoue sur la machine avec
 * « Could not create the temporary save file », et c'est la l'explication
 * probable. Cette sonde la transforme en mesure, en ouvrant le meme fichier de
 * quatre facons.
 */
#include <cstdio>
#include <filesystem>
#include <fstream>

static FILE *g_log;
static void say(const char *what, bool ok)
{
    std::printf("  %-34s : %s\n", what, ok ? "OK" : "ECHEC");
    if (g_log) std::fprintf(g_log, "  %-34s : %s\n", what, ok ? "OK" : "ECHEC");
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
      say("ofstream(litteral etroit)", (bool)f); }
    { FILE *f = std::fopen("D:\\WPROBE.DAT", "rb");
      say("fopen etroit", f != nullptr); if (f) std::fclose(f); }
    { std::ifstream f(p, std::ios::binary);
      say("ifstream(path)", (bool)f); }
    { std::ifstream f(p.string(), std::ios::binary);
      say("ifstream(path.string())", (bool)f); }

    if (g_log) std::fclose(g_log);
    return 0;
}
