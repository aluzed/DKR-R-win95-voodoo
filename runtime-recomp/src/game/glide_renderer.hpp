#pragma once

#include "ultramodern/renderer_context.hpp"

#include <atomic>
#include <cstdint>

#if defined(DKR_TARGET_WIN95)
#include "render/backend.h"
#include "render/f3ddkr.h"
#endif

namespace dkr::runtime {

// The Glide renderer, wired onto the E04 chain.
//
// It replaces `DiagnosticRenderer`, which counted display lists without reading
// them. What changes here: the display list actually traverses the decoder, the
// transform and the clipper, and comes out as triangles on the Voodoo.
//
// Every member function is called from ultramodern's single graphics thread --
// construction included, since `create_render_context` is called inside
// `gfx_thread_func`. That is what makes Glide usable here: the library admits
// only one thread.
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

    // Running totals over the whole session, not per frame. A per-frame counter
    // says nothing useful in a log read after the fact: what one wants to know
    // is whether the chain has emitted anything at all since the start, and
    // where it loses what it decodes.
    unsigned long total_commands_ = 0;
    unsigned long total_triangles_ = 0;
    unsigned long total_emitted_ = 0;
    unsigned long total_rejects_ = 0;
    // Recognised commands whose effect is not wired yet: the share of the frame
    // this port still ignores, and therefore what is left to do.
    unsigned long total_deferred_ = 0;
    // Rectangles actually handed to the backend, as opposed to FILLRECT commands
    // decoded. The gap between the two distinguishes "the game does not ask for
    // any" from "we decode them and lose them".
    unsigned long total_rects_ = 0;
    unsigned long total_states_ = 0;
    unsigned long total_combiners_known_ = 0;
    unsigned long total_combiners_unknown_ = 0;
    // Zero here would mean we are still drawing at an invented scale.
    unsigned long total_viewports_ = 0;
    unsigned long total_tex_loaded_ = 0;
    unsigned long total_tex_reused_ = 0;
    unsigned long total_tex_refused_ = 0;
    unsigned long total_tex_padded_ = 0;
    unsigned long total_emitted_textured_ = 0;
    unsigned long area_[4] = {0};
    unsigned long depth_[4] = {0};
    unsigned long blend_[8] = {0};
    unsigned long total_alpha_test_ = 0;
    unsigned long total_tex_black_ = 0;
    unsigned long total_tex_with_content_ = 0;
    unsigned long emitted_per_combine_[DKR_COMBINE_COUNT] = {0};
    unsigned long total_tex_aspect_ = 0;
    unsigned long total_tex_unsupported_ = 0;
    unsigned long total_tex_out_of_rdram_ = 0;
    unsigned long total_approximate_ = 0;
    unsigned long total_fill_wrong_cycle_ = 0;
    // Broken down by kind, because the total does not say what to fix: an
    // address outside RDRAM accuses the addressing, an unknown opcode accuses
    // the decoding, a vertex index accuses a command missed upstream.
    unsigned long rejects_by_kind_[DKR_F3D_REJECT_COUNT_MAX] = {0};
    // What a DKR frame is made of, opcode by opcode. This is what says what to
    // implement next, rather than a microcode table: the table describes what
    // the microcode *can* emit, the histogram what this game *does* emit.
    unsigned long opcodes_[256] = {0};
#endif
};

std::unique_ptr<ultramodern::renderer::RendererContext> CreateGlideRenderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

// Chooses between Glide and the diagnostic renderer according to `DKR_RENDERER`.
// Defined in game_main.cpp, where both headers are visible.
std::unique_ptr<ultramodern::renderer::RendererContext> SelectRenderContext(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

} // namespace dkr::runtime
