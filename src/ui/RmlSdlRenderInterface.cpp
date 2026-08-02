#include "dkrport/ui/RmlSdlRenderInterface.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace dkrport {
namespace {

SDL_FColor ToSdlColour(const Rml::ColourbPremultiplied& colour) {
    const Rml::Colourb unpremultiplied = colour.ToNonPremultiplied();
    constexpr float scale = 1.0F / 255.0F;
    return {
        static_cast<float>(unpremultiplied.red) * scale,
        static_cast<float>(unpremultiplied.green) * scale,
        static_cast<float>(unpremultiplied.blue) * scale,
        static_cast<float>(unpremultiplied.alpha) * scale};
}

} // namespace

RmlSdlRenderInterface::RmlSdlRenderInterface(SDL_Renderer* renderer)
    : m_renderer(renderer), m_scissorEnabled(false), m_scissor{0, 0, 0, 0}, m_transform(nullptr) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
}

Rml::CompiledGeometryHandle RmlSdlRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                   Rml::Span<const int> indices) {
    auto geometry = std::make_unique<Geometry>();
    geometry->vertices.assign(vertices.begin(), vertices.end());
    geometry->indices.assign(indices.begin(), indices.end());
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
}

void RmlSdlRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometryHandle, Rml::Vector2f translation,
                                           Rml::TextureHandle textureHandle) {
    auto* geometry = reinterpret_cast<Geometry*>(geometryHandle);
    if (!geometry || geometry->vertices.empty() || geometry->indices.empty()) return;

    std::vector<SDL_Vertex> vertices;
    vertices.reserve(geometry->vertices.size());
    for (const Rml::Vertex& vertex : geometry->vertices) {
        SDL_Vertex output{};
        output.position.x = vertex.position.x + translation.x;
        output.position.y = vertex.position.y + translation.y;
        output.color = ToSdlColour(vertex.colour);
        output.tex_coord.x = vertex.tex_coord.x;
        output.tex_coord.y = vertex.tex_coord.y;
        vertices.push_back(output);
    }

    SDL_Texture* texture = reinterpret_cast<SDL_Texture*>(textureHandle);
    SDL_RenderGeometry(m_renderer, texture, vertices.data(), static_cast<int>(vertices.size()),
                       geometry->indices.data(), static_cast<int>(geometry->indices.size()));
}

void RmlSdlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    delete reinterpret_cast<Geometry*>(geometry);
}

Rml::TextureHandle RmlSdlRenderInterface::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) {
    static_cast<void>(source);
    textureDimensions = {0, 0};
    return {};
}

Rml::TextureHandle RmlSdlRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                          Rml::Vector2i sourceDimensions) {
    if (!m_renderer || source.size() == 0 || sourceDimensions.x <= 0 || sourceDimensions.y <= 0) return {};

    // RmlUi provides premultiplied RGBA pixels, while SDL_BLENDMODE_BLEND expects straight alpha.
    // Convert once during texture creation so text and anti-aliased edges retain the intended colour.
    std::vector<Rml::byte> pixels(source.begin(), source.end());
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        const unsigned int alpha = pixels[offset + 3];
        if (alpha == 0U || alpha == 255U) continue;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const unsigned int value = pixels[offset + channel];
            pixels[offset + channel] = static_cast<Rml::byte>(std::min(255U, (value * 255U + alpha / 2U) / alpha));
        }
    }

    SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                             sourceDimensions.x, sourceDimensions.y);
    if (!texture) return {};

    if (!SDL_UpdateTexture(texture, nullptr, pixels.data(), sourceDimensions.x * 4)) {
        SDL_DestroyTexture(texture);
        return {};
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return reinterpret_cast<Rml::TextureHandle>(texture);
}

void RmlSdlRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
    if (texture) SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(texture));
}

void RmlSdlRenderInterface::EnableScissorRegion(bool enable) {
    m_scissorEnabled = enable;
    SDL_SetRenderClipRect(m_renderer, enable ? &m_scissor : nullptr);
}

void RmlSdlRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    m_scissor = {region.Left(), region.Top(), std::max(0, region.Width()), std::max(0, region.Height())};
    if (m_scissorEnabled) SDL_SetRenderClipRect(m_renderer, &m_scissor);
}

void RmlSdlRenderInterface::SetTransform(const Rml::Matrix4f* transform) {
    // The Milestone 0 launcher deliberately avoids CSS transforms. Keeping the pointer records
    // whether one was requested and leaves a clean extension point for a future GPU renderer.
    m_transform = transform;
    static_cast<void>(m_transform);
}

} // namespace dkrport
