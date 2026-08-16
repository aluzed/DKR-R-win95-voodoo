#pragma once

#include "ultramodern/renderer_context.hpp"

#include <atomic>
#include <cstdint>

#if defined(DKR_TARGET_WIN95)
#include "render/backend.h"
#include "render/f3ddkr.h"
#endif

namespace dkr::runtime {

// Le rendu Glide branché sur la chaîne de E04.
//
// Il remplace `DiagnosticRenderer`, qui comptait les display lists sans les
// lire. Ce que change ce contexte : la display list traverse réellement le
// décodeur, la transformation, le découpage, et ressort en triangles sur la
// Voodoo.
//
// **Ce n'est pas encore une image juste**, et il vaut mieux le dire que de le
// laisser découvrir : l'état RDP (E04-S06) et le décodage de textures (E04-S07)
// ne sont pas branchés, et la commande de fenêtre d'affichage du microcode
// (`MOVEMEM`) n'est pas décodée. La géométrie sort, les couleurs et les textures
// suivront.
//
// Toutes les fonctions membres sont appelées depuis le fil graphique unique
// d'ultramodern — construction comprise, puisque `create_render_context` est
// appelé dans `gfx_thread_func`. C'est ce qui rend Glide utilisable ici : la
// bibliothèque n'admet qu'un seul fil.
class GlideRenderer final : public ultramodern::renderer::RendererContext {
public:
    GlideRenderer();

    bool valid() override;
    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override;
    void enable_instant_present() override;
    void send_dl(const OSTask* task, std::uint8_t* rdram_snapshot) override;
    void update_screen() override;
    void shutdown() override;
    std::uint32_t get_display_framerate() const override;
    float get_resolution_scale() const override;

private:
    std::atomic<std::uint64_t> display_list_count_{0};
    std::atomic<std::uint64_t> present_count_{0};

#if defined(DKR_TARGET_WIN95)
    dkr_render_backend backend_{};
    dkr_f3d_context context_{};
    bool opened_ = false;
    int width_ = 0;
    int height_ = 0;

    // Cumuls sur toute la partie, et non par image. Un compteur par image ne dit
    // rien d'utile dans un journal qu'on lit après coup : ce qu'on veut savoir
    // est si la chaîne a émis quoi que ce soit depuis le début, et où elle perd
    // ce qu'elle décode.
    unsigned long total_commands_ = 0;
    unsigned long total_triangles_ = 0;
    unsigned long total_emitted_ = 0;
    unsigned long total_rejects_ = 0;
    // Commandes reconnues dont l'effet n'est pas encore branché : la part de
    // l'image que ce portage ignore encore, et donc ce qui reste à faire.
    unsigned long total_deferred_ = 0;
    // Rectangles réellement remis au backend, par opposition aux commandes
    // FILLRECT décodées. L'écart entre les deux est ce qui distingue « le jeu
    // n'en demande pas » de « on les décode et on les perd ».
    unsigned long total_rects_ = 0;
    unsigned long total_etats_ = 0;
    unsigned long total_comb_connus_ = 0;
    unsigned long total_comb_inconnus_ = 0;
    // Zéro ici signifierait qu'on dessine encore à une échelle inventée.
    unsigned long total_viewports_ = 0;
    unsigned long total_tex_chargees_ = 0;
    unsigned long total_tex_reutilisees_ = 0;
    unsigned long total_tex_refusees_ = 0;
    unsigned long total_tex_remplies_ = 0;
    unsigned long total_emis_texture_ = 0;
    unsigned long aire_[4] = {0};
    unsigned long profondeur_[4] = {0};
    unsigned long melange_[8] = {0};
    unsigned long total_test_alpha_ = 0;
    unsigned long total_tex_noires_ = 0;
    unsigned long total_tex_contenu_ = 0;
    unsigned long emis_par_combine_[DKR_COMBINE_COUNT] = {0};
    unsigned long total_tex_proportions_ = 0;
    unsigned long total_tex_inconnues_ = 0;
    unsigned long total_tex_hors_ = 0;
    unsigned long total_approches_ = 0;
    unsigned long total_fill_hors_cycle_ = 0;
    // Par catégorie, parce que le total ne dit pas quoi corriger : une adresse
    // hors RDRAM accuse l'adressage, un opcode inconnu accuse le décodage, un
    // index de sommet accuse une commande manquée en amont.
    unsigned long rejects_by_kind_[DKR_F3D_REJECT_COUNT_MAX] = {0};
    // De quoi une image de DKR est faite, opcode par opcode. C'est ce qui dit
    // quoi implémenter ensuite, plutôt qu'une table du microcode : celle-ci
    // décrit ce que le microcode peut émettre, l'histogramme ce que ce jeu
    // émet vraiment.
    unsigned long opcodes_[256] = {0};
#endif
};

std::unique_ptr<ultramodern::renderer::RendererContext> CreateGlideRenderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

// Choisit entre Glide et le rendu de diagnostic selon `DKR_RENDERER`.
// Définie dans game_main.cpp, où les deux en-têtes sont visibles.
std::unique_ptr<ultramodern::renderer::RendererContext> SelectRenderContext(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

} // namespace dkr::runtime
