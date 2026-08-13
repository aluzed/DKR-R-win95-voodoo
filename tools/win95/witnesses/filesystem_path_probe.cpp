/* E02-S05 — un objet std::filesystem::path est-il utilisable sous Windows 95 ?
 *
 * Il ne tire qu'un seul symbole bloquant, `LoadLibraryW`, et c'est un bouchon :
 * le binaire se charge donc. Reste a savoir si ce que libstdc++ en fait tient.
 */
#include <filesystem>
#include <cstdio>
int main(){
  FILE *f = fopen("D:\\FSPATH.TXT", "w");
  std::filesystem::path p{"D:\\JEU\\SAUVE.DAT"};
  std::filesystem::path q = p.parent_path();
  std::filesystem::path r = p / "AUTRE.DAT";
  if (f) {
    fprintf(f, "path                : %s\n", p.string().c_str());
    fprintf(f, "parent_path         : %s\n", q.string().c_str());
    fprintf(f, "filename            : %s\n", p.filename().string().c_str());
    fprintf(f, "extension           : %s\n", p.extension().string().c_str());
    fprintf(f, "concatenation       : %s\n", r.string().c_str());
    fprintf(f, "verdict             : %s\n",
            (p.string() == "D:\\JEU\\SAUVE.DAT" && p.filename().string() == "SAUVE.DAT")
              ? "UTILISABLE" : "INCORRECT");
    fclose(f);
  }
  return 0;
}
