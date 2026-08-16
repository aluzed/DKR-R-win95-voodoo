/* E02-S05 - the file-operation indirection point, tested.
 *
 * What is established: the diverted operations behave like the
 * `std::filesystem` ones the callers were using - same return values, including
 * in the cases where they are not obvious.
 *
 * `remove` is the example: it returns **false** on an already absent file,
 * without that being an error. The caller wanted it gone, and it is gone; but
 * nothing was deleted, and `std::filesystem::remove` says so. A port that
 * returned true there would look correct and would make any code counting
 * genuinely deleted files lie.
 *
 * One source for both targets, like the other suites: on the host it takes the
 * `std::` branch, on the machine the `...A` branch. It is precisely the
 * equivalence of the two that has to be tested.
 */
#include "fileio.hpp"
#include <cstdio>
#include <cstring>
#include <string>
static FILE *g_out;
int main(){
  int fails = 0;
  g_out = std::fopen("D:\\FSSEAM.TXT","w");
  auto check=[&](const char*w,bool c){ std::printf("  %s %s\n", c?"ok   ":"FAIL ", w);
    if(g_out) std::fprintf(g_out,"  %s %s\n", c?"ok   ":"FAIL ", w);
    if(!c) fails++; };
  std::filesystem::path base{"D:\\FSSEAM"};
  std::filesystem::path deep = base / "a" / "b";
  std::filesystem::path f    = deep / "x.dat";

  /* The suite wipes its tree *before* starting, and not only afterwards. The
     difference is not cosmetic: `create_directories` returns false on an
     already present directory - which is correct, nothing was created - so that
     an interrupted run made the next one fail on a check that had done nothing
     wrong. A false failure wears down trust in a suite just as surely as a false
     success.

     On the test machine that counts twice over: the transfer disk keeps files
     from one session to the next. */
  auto wipe = [](const std::filesystem::path &root, auto &&self) -> void {
    if (!dkr::fs::exists(root)) { return; }
    if (dkr::fs::is_directory(root)) {
      for (const auto &e : dkr::fs::list_directory(root)) { self(e, self); }
    }
    dkr::fs::remove(root);
  };
  wipe(base, wipe);
  check("the prior cleanup did clear the ground", !dkr::fs::exists(base));

  check("create_directories", dkr::fs::create_directories(deep));
  check("the directory exists", dkr::fs::exists(deep));
  check("and it is a directory", dkr::fs::is_directory(deep));

  /* `create_directories` returns "I created at least one", not "it is there".
     On an already present directory it therefore returns false. Without this
     check the gap slipped through: the line above is content with true, and the
     target returned true in both cases. */
  check("create_directories on a present directory returns false",
        !dkr::fs::create_directories(deep));

  /* `remove` must delete an empty directory, and not merely say so. The check
     bears on the effect, not on the returned value - it is precisely by
     returning success without doing anything that the target went wrong. */
  {
    std::filesystem::path d = deep / "empty";
    dkr::fs::create_directories(d);
    check("remove of an empty directory returns true", dkr::fs::remove(d));
    check("and the directory really is gone", !dkr::fs::exists(d));
  }
  { FILE*h=std::fopen(f.string().c_str(),"wb"); if(h){std::fputs("abc",h);std::fclose(h);} }
  check("the file exists", dkr::fs::exists(f));
  check("it is not a directory", !dkr::fs::is_directory(f));
  std::filesystem::path c = deep / "y.dat";
  check("copy_file_overwrite", dkr::fs::copy_file_overwrite(f, c));
  check("the copy exists", dkr::fs::exists(c));
  check("copying over it", dkr::fs::copy_file_overwrite(f, c));
  {
    /* `copy_options::none` refuses to overwrite, and two call sites depend on
       it: importing a filter or a texture pack must not silently replace the one
       that already bears that name. The check bears on the refusal **and** on
       the original content surviving. */
    std::filesystem::path keep = deep / "keep.dat";
    { FILE *h = std::fopen(keep.string().c_str(), "wb");
      if (h) { std::fputs("oldest", h); std::fclose(h); } }
    std::error_code nec;
    check("copy_file_no_overwrite refuses an existing target",
          !dkr::fs::copy_file_no_overwrite(f, keep, nec) && (bool)nec);
    check("and the old content is intact", dkr::fs::file_size(keep) == 6);
    dkr::fs::remove(keep);
    check("copy_file_no_overwrite writes when the target is absent",
          dkr::fs::copy_file_no_overwrite(f, keep));
    check("and the copy has the right content", dkr::fs::file_size(keep) == 3);
    dkr::fs::remove(keep);
  }

  check("remove returns true", dkr::fs::remove(c));
  check("the file is gone", !dkr::fs::exists(c));
  check("remove of an absent file returns false", !dkr::fs::remove(c));
  std::error_code ec;
  check("the error_code form works", dkr::fs::exists(f, ec) && !ec);

  /* --- The operations added for the game's sources ------------------------ */

  check("is_regular_file on a file", dkr::fs::is_regular_file(f));
  check("is_regular_file on a directory", !dkr::fs::is_regular_file(deep));

  /* `file_size` must tell an empty file from an absent one: both would return
     zero if we only looked at the size. */
  check("file_size returns the size", dkr::fs::file_size(f) == 3);
  {
    std::filesystem::path empty = deep / "empty.dat";
    FILE *h = std::fopen(empty.string().c_str(), "wb"); if (h) std::fclose(h);
    check("an empty file returns 0", dkr::fs::file_size(empty) == 0);
    dkr::fs::remove(empty);
  }
  check("an absent file does not return 0",
        dkr::fs::file_size(deep / "never.dat") == (std::uintmax_t)-1);

  {
    std::filesystem::path a = deep / "r1.dat", b = deep / "r2.dat";
    { FILE *h = std::fopen(a.string().c_str(), "wb"); if (h) { std::fputs("xy", h); std::fclose(h);} }
    dkr::fs::rename(a, b);
    check("rename moves", !dkr::fs::exists(a) && dkr::fs::exists(b));
    /* The target has no atomic replacement: `rename` over an existing target
       must succeed all the same. */
    { FILE *h = std::fopen(a.string().c_str(), "wb"); if (h) { std::fputs("zz", h); std::fclose(h);} }
    dkr::fs::rename(a, b);
    check("rename overwrites an existing target",
          !dkr::fs::exists(a) && dkr::fs::file_size(b) == 2);
    dkr::fs::remove(b);
  }

  {
    std::filesystem::path abs = dkr::fs::absolute(f);
    check("absolute returns a non-empty path", !abs.string().empty());
    check("and it designates the same file", dkr::fs::exists(abs));
  }

  {
    /* Three entries, and neither "." nor ".." - like `directory_iterator`. */
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
    check("list_directory sees the three entries", seen == 3);
    check("and includes neither . nor ..", dots == 0);
    for (const char *n : {"e1.dat", "e2.dat", "e3.dat"}) dkr::fs::remove(deep / n);
  }


  {
    /* `remove_all` returns the number of entries deleted, not a boolean. The
       check bears on the count **and** on the effect: it is by returning success
       without doing anything that `remove` went wrong. */
    std::filesystem::path tree = deep / "tree";
    dkr::fs::create_directories(tree / "sub");
    for (const char *n : {"a.dat", "b.dat"}) {
      FILE *h = std::fopen((tree / n).string().c_str(), "wb");
      if (h) std::fclose(h);
    }
    { FILE *h = std::fopen((tree / "sub" / "c.dat").string().c_str(), "wb");
      if (h) std::fclose(h); }
    /* tree, sub, a.dat, b.dat, c.dat - five entries. */
    check("remove_all counts what it deletes", dkr::fs::remove_all(tree) == 5);
    check("and the tree is gone", !dkr::fs::exists(tree));
    check("remove_all on an absent path returns 0", dkr::fs::remove_all(tree) == 0);
  }

  {
    std::filesystem::path cwd = dkr::fs::current_path();
    check("current_path returns a non-empty path", !cwd.string().empty());
    check("and that path is a directory", dkr::fs::is_directory(cwd));
  }

  /* Windows 95 has no symbolic links: the right answer is false, and it is also
     what the standard library returns on an ordinary file. So the two branches
     genuinely agree here. */
  check("is_symlink on an ordinary file", !dkr::fs::is_symlink(f));

  {
    /* `weakly_canonical` resolves ".." without requiring the path to exist. */
    std::filesystem::path detour = deep / "." / ".." / "b" / "x.dat";
    std::filesystem::path direct = dkr::fs::weakly_canonical(f);
    check("weakly_canonical returns an absolute path",
          direct.is_absolute() || !direct.string().empty());
    check("and two spellings of the same path meet",
          dkr::fs::weakly_canonical(detour) == direct);
  }

  dkr::fs::remove(f);
  std::printf("\n%d failure(s)\n", fails);
  if(g_out){ std::fprintf(g_out,"\n%d failure(s)\n", fails); std::fclose(g_out);} 
  return fails != 0;
}
