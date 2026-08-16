/* E02-S05 - is a std::filesystem::path object usable under Windows 95?
 *
 * It pulls in only one blocking symbol, `LoadLibraryW`, and that one is a stub:
 * so the binary loads. What remains is whether what libstdc++ makes of it holds.
 */
#include <filesystem>
#include <cstdio>
int main(){
  FILE *f = fopen("D:\\FSPATH.TXT", "w");
  std::filesystem::path p{"D:\\GAME\\SAVE.DAT"};
  std::filesystem::path q = p.parent_path();
  std::filesystem::path r = p / "OTHER.DAT";
  if (f) {
    fprintf(f, "path                : %s\n", p.string().c_str());
    fprintf(f, "parent_path         : %s\n", q.string().c_str());
    fprintf(f, "filename            : %s\n", p.filename().string().c_str());
    fprintf(f, "extension           : %s\n", p.extension().string().c_str());
    fprintf(f, "concatenation       : %s\n", r.string().c_str());
    fprintf(f, "verdict             : %s\n",
            (p.string() == "D:\\GAME\\SAVE.DAT" && p.filename().string() == "SAVE.DAT")
              ? "USABLE" : "INCORRECT");
    fclose(f);
  }
  return 0;
}
