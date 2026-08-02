#pragma once

#include "dkrport/app/Application.h"
#include "dkrport/game/BootSession.h"
#include "dkrport/input/InputManager.h"

#include <future>
#include <memory>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Window;
struct SDL_Process;
union SDL_Event;

namespace Rml {
class Context;
class ElementDocument;
class Element;
}

namespace dkrport {
struct FileDialogState;
class RmlEventListener;
class RmlSdlRenderInterface;
class RmlSdlSystemInterface;

class NativeLauncher {
  public:
    explicit NativeLauncher(Application& application);
    ~NativeLauncher();

    int Run();

  private:
    bool InitialiseWindow(std::string& error);
    bool InitialiseUi(std::string& error);
    void Shutdown();
    void HandleEvent(SDL_Event& event);
    void HandleAction(const std::string& action);
    void SelectRom();
    void StartRomValidation(const std::string& path);
    void PollBackgroundWork();
    void UpdateGameRuntime();
    void StartGame();
    void StopGame();
    void RefreshGamePage();
    void BuildControlsPage();
    void RefreshControlsPage();
    void BeginBindingCapture(const std::string& actionId, const std::string& slotId);
    void RunSelfTest();
    void ResetGameData();
    void OpenPath(const std::string& which);
    void ShowPage(const std::string& pageId);
    void ShowMessage(const std::string& title, const std::string& message, bool error);
    void HideMessage();
    void RefreshStatus();
    void SetText(const std::string& elementId, const std::string& text);
    void SetClass(const std::string& elementId, const std::string& className, bool enabled);
    void SetVisible(const std::string& elementId, bool visible);
    void BindAction(const std::string& elementId, const std::string& action);
    void FocusFirstAction();
    void MoveFocus(int direction);
    void ActivateFocused();
    [[nodiscard]] std::vector<std::string> ActiveActionIds() const;
    bool LoadSystemFont(std::string& error);
    std::string FindSystemFont() const;
    std::string AssetPath(const std::string& relative) const;
    void RenderFrame();

    Application& m_application;
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    Rml::Context* m_context;
    Rml::ElementDocument* m_document;
    std::unique_ptr<RmlSdlSystemInterface> m_systemInterface;
    std::unique_ptr<RmlSdlRenderInterface> m_renderInterface;
    std::unique_ptr<input::InputManager> m_inputManager;
    std::unique_ptr<game::BootSession> m_bootSession;
    std::vector<std::unique_ptr<RmlEventListener>> m_listeners;
    std::vector<unsigned char> m_fontData;
    std::future<RomOperationResult> m_romFuture;
    std::shared_ptr<FileDialogState> m_fileDialogState;
    bool m_fileDialogPending;
    bool m_romWorkPending;
    bool m_running;
    bool m_sdlInitialised;
    bool m_rmlInitialised;
    bool m_messageVisible;
    bool m_processingVisible;
    bool m_gameRunning;
    SDL_Process* m_gameProcess;
    std::uint64_t m_lastGameTickNs;
    std::uint64_t m_lastGameUiUpdateNs;
    std::string m_activePage;
};

} // namespace dkrport
