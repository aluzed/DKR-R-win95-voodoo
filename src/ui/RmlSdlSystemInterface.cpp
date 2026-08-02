#include "dkrport/ui/RmlSdlSystemInterface.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Log.h>

#include <algorithm>
#include <cctype>

namespace dkrport {
namespace {

int ConvertMouseButton(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        default: return 3;
    }
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

RmlSdlSystemInterface::RmlSdlSystemInterface(SDL_Window* window, Logger& logger)
    : m_window(window), m_logger(logger) {
    m_cursors.emplace("arrow", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT));
    m_cursors.emplace("move", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE));
    m_cursors.emplace("pointer", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER));
    m_cursors.emplace("resize", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE));
    m_cursors.emplace("cross", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR));
    m_cursors.emplace("text", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT));
    m_cursors.emplace("unavailable", SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED));
}

RmlSdlSystemInterface::~RmlSdlSystemInterface() {
    for (auto& [name, cursor] : m_cursors) {
        static_cast<void>(name);
        if (cursor) SDL_DestroyCursor(cursor);
    }
}

double RmlSdlSystemInterface::GetElapsedTime() {
    return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0;
}

bool RmlSdlSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT: m_logger.Error("RmlUi: " + message); break;
        case Rml::Log::LT_WARNING: m_logger.Warning("RmlUi: " + message); break;
        default: m_logger.Info("RmlUi: " + message); break;
    }
    return true;
}

void RmlSdlSystemInterface::SetMouseCursor(const Rml::String& cursorName) {
    std::string name = Lower(cursorName.empty() ? "arrow" : cursorName);
    if (name.rfind("rmlui-scroll", 0U) == 0U) name = "move";
    const auto found = m_cursors.find(name);
    if (found != m_cursors.end() && found->second) SDL_SetCursor(found->second);
}

void RmlSdlSystemInterface::SetClipboardText(const Rml::String& text) {
    SDL_SetClipboardText(text.c_str());
}

void RmlSdlSystemInterface::GetClipboardText(Rml::String& text) {
    char* raw = SDL_GetClipboardText();
    text = raw ? raw : "";
    SDL_free(raw);
}

void RmlSdlSystemInterface::ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight) {
    if (!m_window) return;
    const SDL_Rect area = {
        static_cast<int>(caretPosition.x), static_cast<int>(caretPosition.y), 1, static_cast<int>(lineHeight)};
    SDL_SetTextInputArea(m_window, &area, 0);
    SDL_StartTextInput(m_window);
}

void RmlSdlSystemInterface::DeactivateKeyboard() {
    if (m_window) SDL_StopTextInput(m_window);
}

bool RmlSdlSystemInterface::ProcessEvent(Rml::Context* context, SDL_Window* window, SDL_Event& event) {
    if (!context || !window) return true;

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const float density = SDL_GetWindowPixelDensity(window);
            return context->ProcessMouseMove(static_cast<int>(event.motion.x * density),
                                             static_cast<int>(event.motion.y * density), KeyModifiers());
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            SDL_CaptureMouse(true);
            return context->ProcessMouseButtonDown(ConvertMouseButton(event.button.button), KeyModifiers());
        case SDL_EVENT_MOUSE_BUTTON_UP:
            SDL_CaptureMouse(false);
            return context->ProcessMouseButtonUp(ConvertMouseButton(event.button.button), KeyModifiers());
        case SDL_EVENT_MOUSE_WHEEL:
            return context->ProcessMouseWheel({-event.wheel.x, -event.wheel.y}, KeyModifiers());
        case SDL_EVENT_KEY_DOWN: {
            bool result = context->ProcessKeyDown(ConvertKey(event.key.key), KeyModifiers());
            if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                result = context->ProcessTextInput('\n') && result;
            }
            return result;
        }
        case SDL_EVENT_KEY_UP:
            return context->ProcessKeyUp(ConvertKey(event.key.key), KeyModifiers());
        case SDL_EVENT_TEXT_INPUT:
            return context->ProcessTextInput(Rml::String(&event.text.text[0]));
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            context->SetDimensions({event.window.data1, event.window.data2});
            return true;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(window));
            return true;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            context->ProcessMouseLeave();
            return true;
        default: return true;
    }
}

Rml::Input::KeyIdentifier RmlSdlSystemInterface::ConvertKey(SDL_Keycode key) {
    using namespace Rml::Input;
    switch (key) {
        case SDLK_UNKNOWN: return KI_UNKNOWN;
        case SDLK_ESCAPE: return KI_ESCAPE;
        case SDLK_SPACE: return KI_SPACE;
        case SDLK_0: return KI_0;
        case SDLK_1: return KI_1;
        case SDLK_2: return KI_2;
        case SDLK_3: return KI_3;
        case SDLK_4: return KI_4;
        case SDLK_5: return KI_5;
        case SDLK_6: return KI_6;
        case SDLK_7: return KI_7;
        case SDLK_8: return KI_8;
        case SDLK_9: return KI_9;
        case SDLK_A: return KI_A;
        case SDLK_B: return KI_B;
        case SDLK_C: return KI_C;
        case SDLK_D: return KI_D;
        case SDLK_E: return KI_E;
        case SDLK_F: return KI_F;
        case SDLK_G: return KI_G;
        case SDLK_H: return KI_H;
        case SDLK_I: return KI_I;
        case SDLK_J: return KI_J;
        case SDLK_K: return KI_K;
        case SDLK_L: return KI_L;
        case SDLK_M: return KI_M;
        case SDLK_N: return KI_N;
        case SDLK_O: return KI_O;
        case SDLK_P: return KI_P;
        case SDLK_Q: return KI_Q;
        case SDLK_R: return KI_R;
        case SDLK_S: return KI_S;
        case SDLK_T: return KI_T;
        case SDLK_U: return KI_U;
        case SDLK_V: return KI_V;
        case SDLK_W: return KI_W;
        case SDLK_X: return KI_X;
        case SDLK_Y: return KI_Y;
        case SDLK_Z: return KI_Z;
        case SDLK_BACKSPACE: return KI_BACK;
        case SDLK_TAB: return KI_TAB;
        case SDLK_RETURN: return KI_RETURN;
        case SDLK_KP_ENTER: return KI_NUMPADENTER;
        case SDLK_PAGEUP: return KI_PRIOR;
        case SDLK_PAGEDOWN: return KI_NEXT;
        case SDLK_END: return KI_END;
        case SDLK_HOME: return KI_HOME;
        case SDLK_LEFT: return KI_LEFT;
        case SDLK_UP: return KI_UP;
        case SDLK_RIGHT: return KI_RIGHT;
        case SDLK_DOWN: return KI_DOWN;
        case SDLK_INSERT: return KI_INSERT;
        case SDLK_DELETE: return KI_DELETE;
        case SDLK_LSHIFT: return KI_LSHIFT;
        case SDLK_RSHIFT: return KI_RSHIFT;
        case SDLK_LCTRL: return KI_LCONTROL;
        case SDLK_RCTRL: return KI_RCONTROL;
        case SDLK_LALT: return KI_LMENU;
        case SDLK_RALT: return KI_RMENU;
        case SDLK_LGUI: return KI_LMETA;
        case SDLK_RGUI: return KI_RMETA;
        default: return KI_UNKNOWN;
    }
}

int RmlSdlSystemInterface::KeyModifiers() {
    const SDL_Keymod modifiers = SDL_GetModState();
    int result = 0;
    if ((modifiers & SDL_KMOD_CTRL) != 0) result |= Rml::Input::KM_CTRL;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) result |= Rml::Input::KM_SHIFT;
    if ((modifiers & SDL_KMOD_ALT) != 0) result |= Rml::Input::KM_ALT;
    if ((modifiers & SDL_KMOD_NUM) != 0) result |= Rml::Input::KM_NUMLOCK;
    if ((modifiers & SDL_KMOD_CAPS) != 0) result |= Rml::Input::KM_CAPSLOCK;
    return result;
}

} // namespace dkrport
