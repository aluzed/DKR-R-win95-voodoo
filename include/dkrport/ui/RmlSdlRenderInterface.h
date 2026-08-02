#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <SDL3/SDL.h>

#include <vector>

namespace dkrport {

class RmlSdlRenderInterface final : public Rml::RenderInterface {
  public:
    explicit RmlSdlRenderInterface(SDL_Renderer* renderer);
    ~RmlSdlRenderInterface() override = default;

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

  private:
    struct Geometry {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };

    SDL_Renderer* m_renderer;
    bool m_scissorEnabled;
    SDL_Rect m_scissor;
    const Rml::Matrix4f* m_transform;
};

} // namespace dkrport
