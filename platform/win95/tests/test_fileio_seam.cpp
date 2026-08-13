/* E02-S05 — epreuve du point d'indirection des operations de fichiers.
 *
 * Ce qui est etabli : les operations detournees se comportent comme celles de
 * `std::filesystem` que les appelants employaient — memes valeurs de retour, y
 * compris dans les cas ou elles ne sont pas evidentes.
 *
 * `remove` en est l'exemple : elle rend **false** sur un fichier deja absent,
 * sans que ce soit une erreur. L'appelant voulait qu'il ne soit plus la, il ne
 * l'est pas ; mais rien n'a ete efface, et `std::filesystem::remove` le dit
 * ainsi. Un portage qui rendrait true la aurait l'air correct et ferait mentir
 * tout code qui compte les fichiers reellement supprimes.
 *
 * Une seule source pour les deux cibles, comme les autres suites : sur l'hote
 * elle emprunte la branche `std::`, sur la machine la branche `...A`. C'est
 * precisement l'equivalence des deux qu'il faut eprouver.
 */
#include "fileio.hpp"
#include <cstdio>
#include <cstring>
static FILE *g_out;
int main(){
  int fails = 0;
  g_out = std::fopen("D:\\FSSEAM.TXT","w");
  auto check=[&](const char*w,bool c){ std::printf("  %s %s\n", c?"ok   ":"ECHEC", w);
    if(g_out) std::fprintf(g_out,"  %s %s\n", c?"ok   ":"ECHEC", w);
    if(!c) fails++; };
  std::filesystem::path base{"D:\\FSSEAM"};
  std::filesystem::path deep = base / "a" / "b";
  std::filesystem::path f    = deep / "x.dat";

  check("create_directories", dkr::fs::create_directories(deep));
  check("le repertoire existe", dkr::fs::exists(deep));
  check("et c'est un repertoire", dkr::fs::is_directory(deep));
  { FILE*h=std::fopen(f.string().c_str(),"wb"); if(h){std::fputs("abc",h);std::fclose(h);} }
  check("le fichier existe", dkr::fs::exists(f));
  check("ce n'est pas un repertoire", !dkr::fs::is_directory(f));
  std::filesystem::path c = deep / "y.dat";
  check("copy_file_overwrite", dkr::fs::copy_file_overwrite(f, c));
  check("la copie existe", dkr::fs::exists(c));
  check("copie par-dessus", dkr::fs::copy_file_overwrite(f, c));
  check("remove rend true", dkr::fs::remove(c));
  check("le fichier a disparu", !dkr::fs::exists(c));
  check("remove d'un absent rend false", !dkr::fs::remove(c));
  std::error_code ec;
  check("la forme a error_code marche", dkr::fs::exists(f, ec) && !ec);
  dkr::fs::remove(f);
  std::printf("\n%d echec(s)\n", fails);
  if(g_out){ std::fprintf(g_out,"\n%d echec(s)\n", fails); std::fclose(g_out);} 
  return fails != 0;
}
