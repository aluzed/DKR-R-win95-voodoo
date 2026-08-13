/* E02-S05 — les signatures employees par les sources du jeu.
 *
 * Quatre fichiers du jeu — `runtime_ui`, `runtime_texture_packs`,
 * `runtime_crt_overlay`, `runtime_rice_texture_import` — ne se compilent que
 * lorsque RT64 est present. Il ne l'est pas dans ce depot, et Windows 95 ne les
 * construit de toute facon jamais : RT64 exige D3D12, Vulkan ou Metal.
 *
 * Leurs appels au point d'indirection ne peuvent donc pas etre eprouves par une
 * compilation de ces fichiers. Ce qui *peut* l'etre, et qui porte le risque
 * reel, c'est que le point d'indirection offre exactement les formes qu'ils
 * emploient : les surcharges a `error_code` surtout, dont l'absence ne se voit
 * qu'a la compilation.
 *
 * Chaque appel ci-dessous est repris du code, avec les memes types d'arguments.
 * Rien n'est execute : c'est la compilation qui est le controle, et elle a lieu
 * pour les deux branches.
 */
#include "fileio.hpp"

#include <cstdint>
#include <filesystem>
#include <system_error>
#include <vector>

/* Jamais appelee : seule sa compilation compte. */
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

    /* --- save_manager.cpp, virtual_pak.cpp (deja compiles pour la cible) --- */
    (void)dkr::fs::copy_file_overwrite(p, q, ec);
    (void)dkr::fs::create_directories(p);
    (void)dkr::fs::remove_all(p);
    (void)dkr::fs::list_directory(p);
}
