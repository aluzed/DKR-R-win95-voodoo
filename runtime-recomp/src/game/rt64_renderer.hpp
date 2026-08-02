#pragma once

#include "f3ddkr_rt64.hpp"
#include "ultramodern/renderer_context.hpp"

#include <cstdint>
#include <memory>

namespace RT64 {
struct Application;
}

namespace dkr::runtime {

class RT64Renderer final : public ultramodern::renderer::RendererContext {
public:
    RT64Renderer(std::uint8_t* rdram,
                 ultramodern::renderer::WindowHandle window_handle,
                 bool developer_mode);
    ~RT64Renderer() override;

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
    std::unique_ptr<RT64::Application> application_;
    F3DDKRRT64Bridge f3ddkr_;
    std::uint64_t present_count_ = 0;
};

std::unique_ptr<ultramodern::renderer::RendererContext> CreateRT64Renderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode);

} // namespace dkr::runtime
