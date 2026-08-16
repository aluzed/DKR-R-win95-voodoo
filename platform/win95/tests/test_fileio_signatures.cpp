/* E02-S05 - the signatures the game's sources use.
 *
 * Four of the game's files - `runtime_ui`, `runtime_texture_packs`,
 * `runtime_crt_overlay`, `runtime_rice_texture_import` - only compile when RT64
 * is present. It is not present in this repository, and Windows 95 never builds
 * them anyway: RT64 requires D3D12, Vulkan or Metal.
 *
 * Their calls into the indirection point therefore cannot be tested by compiling
 * those files. What *can* be tested, and what carries the real risk, is that the
 * indirection point offers exactly the forms they use: the `error_code`
 * overloads above all, whose absence only shows at compile time.
 *
 * Every call below is taken from the code, with the same argument types. Nothing
 * is executed: compilation is the check, and it happens for both branches.
 */
#include "fileio.hpp"

#include <cstdint>
#include <filesystem>
#include <system_error>
#include <vector>

/* Never called: only its compilation counts. */
void dkr_fileio_signatures_used_by_the_game(void)
{
    const std::filesystem::path p{"a"}, q{"b"};
    std::error_code ec;

    /* --- runtime_ui.cpp ---------------------------------------------------- */
    (void)dkr::fs::create_directories(p, ec);
    (void)dkr::fs::list_directory(p, ec);
    for (const auto &entry : dkr::fs::list_directory(p, ec)) {
        (void)dkr::fs::is_regular_file(entry);
        (void)dkr::fs::is_directory(entry);
    }
    (void)dkr::fs::copy_file_no_overwrite(p, q, ec);
    (void)dkr::fs::remove(p, ec);
    (void)dkr::fs::rename(p, q, ec);
    (void)dkr::fs::exists(p, ec);
    (void)dkr::fs::is_directory(p, ec);
    (void)dkr::fs::is_regular_file(p, ec);
    (void)dkr::fs::file_size(p, ec);
    (void)dkr::fs::absolute(p, ec).lexically_normal();
    (void)(dkr::fs::current_path() / p);
    (void)dkr::fs::current_path(ec);

    /* --- runtime_texture_packs.cpp ---------------------------------------- */
    (void)dkr::fs::is_symlink(p);
    (void)dkr::fs::remove_all(p, ec);
    const std::uintmax_t size = dkr::fs::file_size(p, ec);
    (void)size;

    /* --- runtime_crt_overlay.cpp ------------------------------------------ */
    (void)dkr::fs::weakly_canonical(p).u8string();

    /* --- save_manager.cpp, virtual_pak.cpp (already compiled for the target) - */
    (void)dkr::fs::copy_file_overwrite(p, q, ec);
    (void)dkr::fs::create_directories(p);
    (void)dkr::fs::remove_all(p);
    (void)dkr::fs::list_directory(p);
}
