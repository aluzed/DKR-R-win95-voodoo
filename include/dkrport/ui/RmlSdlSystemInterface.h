#pragma once

#include "dkrport/core/Logger.h"

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>

namespace dkrport {

class RmlSdlSystemInterface final : public Rml::SystemInterface {
  public:
    RmlSdlSystemInterface(SDL_Window* window, Logger& logger);
    ~RmlSdlSystemInterface() override;

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void SetMouseCursor(const Rml::String& cursorName) override;
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;
    void ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) override;
    void DeactivateKeyboard() override;

    static bool ProcessEvent(Rml::Context* context, SDL_Window* window, SDL_Event& event);
    static Rml::Input::KeyIdentifier ConvertKey(SDL_Keycode key);
    static int KeyModifiers();

  private:
    SDL_Window* m_window;
    Logger& m_logger;
    std::unordered_map<std::string, SDL_Cursor*> m_cursors;
};

} // namespace dkrport
