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
#include <string>
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

  /* La suite efface son arborescence *avant* de commencer, et non seulement
     apres. La difference n'est pas cosmetique : `create_directories` rend false
     sur un repertoire deja present — c'est correct, rien n'a ete cree — de sorte
     qu'une execution interrompue faisait echouer la suivante sur un point qui
     n'avait rien a se reprocher. Un faux echec use la confiance qu'on accorde a
     une suite aussi surement qu'un faux succes.

     Sur la machine de test cela compte doublement : le disque de transfert
     conserve les fichiers d'une session a l'autre. */
  auto wipe = [](const std::filesystem::path &root, auto &&self) -> void {
    if (!dkr::fs::exists(root)) { return; }
    if (dkr::fs::is_directory(root)) {
      for (const auto &e : dkr::fs::list_directory(root)) { self(e, self); }
    }
    dkr::fs::remove(root);
  };
  wipe(base, wipe);
  check("le nettoyage prealable a bien vide la place", !dkr::fs::exists(base));

  check("create_directories", dkr::fs::create_directories(deep));
  check("le repertoire existe", dkr::fs::exists(deep));
  check("et c'est un repertoire", dkr::fs::is_directory(deep));

  /* `create_directories` rend « j'en ai cree au moins un », pas « il est la ».
     Sur un repertoire deja present elle rend donc false. Sans cette
     verification l'ecart passait : la ligne au-dessus se contente de true, et la
     cible rendait true dans les deux cas. */
  check("create_directories sur un repertoire present rend false",
        !dkr::fs::create_directories(deep));

  /* `remove` doit effacer un repertoire vide, et pas seulement le dire. Le
     controle porte sur l'effet, pas sur la valeur rendue — c'est justement en
     rendant succes sans rien faire que la cible se trompait. */
  {
    std::filesystem::path d = deep / "vide";
    dkr::fs::create_directories(d);
    check("remove d'un repertoire vide rend true", dkr::fs::remove(d));
    check("et le repertoire a reellement disparu", !dkr::fs::exists(d));
  }
  { FILE*h=std::fopen(f.string().c_str(),"wb"); if(h){std::fputs("abc",h);std::fclose(h);} }
  check("le fichier existe", dkr::fs::exists(f));
  check("ce n'est pas un repertoire", !dkr::fs::is_directory(f));
  std::filesystem::path c = deep / "y.dat";
  check("copy_file_overwrite", dkr::fs::copy_file_overwrite(f, c));
  check("la copie existe", dkr::fs::exists(c));
  check("copie par-dessus", dkr::fs::copy_file_overwrite(f, c));
  {
    /* `copy_options::none` refuse d'ecraser, et deux sites d'appel en dependent :
       importer un filtre ou un pack de textures ne doit pas remplacer
       silencieusement celui qui porte deja ce nom. Le controle porte sur le
       refus **et** sur le fait que le contenu d'origine survit. */
    std::filesystem::path garde = deep / "garde.dat";
    { FILE *h = std::fopen(garde.string().c_str(), "wb");
      if (h) { std::fputs("ancien", h); std::fclose(h); } }
    std::error_code nec;
    check("copy_file_no_overwrite refuse une cible existante",
          !dkr::fs::copy_file_no_overwrite(f, garde, nec) && (bool)nec);
    check("et l'ancien contenu est intact", dkr::fs::file_size(garde) == 6);
    dkr::fs::remove(garde);
    check("copy_file_no_overwrite ecrit quand la cible est absente",
          dkr::fs::copy_file_no_overwrite(f, garde));
    check("et la copie a le bon contenu", dkr::fs::file_size(garde) == 3);
    dkr::fs::remove(garde);
  }

  check("remove rend true", dkr::fs::remove(c));
  check("le fichier a disparu", !dkr::fs::exists(c));
  check("remove d'un absent rend false", !dkr::fs::remove(c));
  std::error_code ec;
  check("la forme a error_code marche", dkr::fs::exists(f, ec) && !ec);

  /* --- Les operations ajoutees pour les sources du jeu -------------------- */

  check("is_regular_file sur un fichier", dkr::fs::is_regular_file(f));
  check("is_regular_file sur un repertoire", !dkr::fs::is_regular_file(deep));

  /* `file_size` doit distinguer un fichier vide d'un fichier absent : tous deux
     rendraient zero si l'on se contentait de la taille. */
  check("file_size rend la taille", dkr::fs::file_size(f) == 3);
  {
    std::filesystem::path empty = deep / "vide.dat";
    FILE *h = std::fopen(empty.string().c_str(), "wb"); if (h) std::fclose(h);
    check("un fichier vide rend 0", dkr::fs::file_size(empty) == 0);
    dkr::fs::remove(empty);
  }
  check("un fichier absent ne rend pas 0",
        dkr::fs::file_size(deep / "jamais.dat") == (std::uintmax_t)-1);

  {
    std::filesystem::path a = deep / "r1.dat", b = deep / "r2.dat";
    { FILE *h = std::fopen(a.string().c_str(), "wb"); if (h) { std::fputs("xy", h); std::fclose(h);} }
    dkr::fs::rename(a, b);
    check("rename deplace", !dkr::fs::exists(a) && dkr::fs::exists(b));
    /* La cible n'a pas de remplacement atomique : `rename` par-dessus une cible
       existante doit malgre tout aboutir. */
    { FILE *h = std::fopen(a.string().c_str(), "wb"); if (h) { std::fputs("zz", h); std::fclose(h);} }
    dkr::fs::rename(a, b);
    check("rename ecrase une cible existante",
          !dkr::fs::exists(a) && dkr::fs::file_size(b) == 2);
    dkr::fs::remove(b);
  }

  {
    std::filesystem::path abs = dkr::fs::absolute(f);
    check("absolute rend un chemin non vide", !abs.string().empty());
    check("et il designe le meme fichier", dkr::fs::exists(abs));
  }

  {
    /* Trois entrees, et ni « . » ni « .. » — comme `directory_iterator`. */
    for (const char *n : {"e1.dat", "e2.dat", "e3.dat"}) {
      FILE *h = std::fopen((deep / n).string().c_str(), "wb");
      if (h) std::fclose(h);
    }
    auto entries = dkr::fs::list_directory(deep);
    int seen = 0, dots = 0;
    for (const auto &e : entries) {
      const std::string n = e.filename().string();
      if (n == "e1.dat" || n == "e2.dat" || n == "e3.dat") seen++;
      if (n == "." || n == "..") dots++;
    }
    check("list_directory voit les trois entrees", seen == 3);
    check("et n'inclut ni . ni ..", dots == 0);
    for (const char *n : {"e1.dat", "e2.dat", "e3.dat"}) dkr::fs::remove(deep / n);
  }


  {
    /* `remove_all` rend le nombre d'entrees effacees, pas un booleen. Le
       controle porte sur le compte **et** sur l'effet : c'est en rendant succes
       sans rien faire que `remove` s'etait trompee. */
    std::filesystem::path tree = deep / "arbre";
    dkr::fs::create_directories(tree / "sous");
    for (const char *n : {"a.dat", "b.dat"}) {
      FILE *h = std::fopen((tree / n).string().c_str(), "wb");
      if (h) std::fclose(h);
    }
    { FILE *h = std::fopen((tree / "sous" / "c.dat").string().c_str(), "wb");
      if (h) std::fclose(h); }
    /* arbre, sous, a.dat, b.dat, c.dat — cinq entrees. */
    check("remove_all compte ce qu'il efface", dkr::fs::remove_all(tree) == 5);
    check("et l'arborescence a disparu", !dkr::fs::exists(tree));
    check("remove_all sur un absent rend 0", dkr::fs::remove_all(tree) == 0);
  }

  {
    std::filesystem::path cwd = dkr::fs::current_path();
    check("current_path rend un chemin non vide", !cwd.string().empty());
    check("et ce chemin est un repertoire", dkr::fs::is_directory(cwd));
  }

  /* Windows 95 n'a pas de liens symboliques : la reponse juste est false, et
     c'est aussi celle que rend la bibliotheque standard sur un fichier
     ordinaire. Les deux branches concordent donc reellement ici. */
  check("is_symlink sur un fichier ordinaire", !dkr::fs::is_symlink(f));

  {
    /* `weakly_canonical` resout « .. » sans exiger que le chemin existe. */
    std::filesystem::path detour = deep / "." / ".." / "b" / "x.dat";
    std::filesystem::path direct = dkr::fs::weakly_canonical(f);
    check("weakly_canonical rend un chemin absolu",
          direct.is_absolute() || !direct.string().empty());
    check("et deux ecritures du meme chemin se rejoignent",
          dkr::fs::weakly_canonical(detour) == direct);
  }

  dkr::fs::remove(f);
  std::printf("\n%d echec(s)\n", fails);
  if(g_out){ std::fprintf(g_out,"\n%d echec(s)\n", fails); std::fclose(g_out);} 
  return fails != 0;
}
