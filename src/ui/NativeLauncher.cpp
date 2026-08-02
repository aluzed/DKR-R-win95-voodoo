#include "dkrport/ui/NativeLauncher.h"

#include "dkrport/Version.h"
#include "dkrport/core/FileUtil.h"
#include "dkrport/ui/RmlEventListener.h"
#include "dkrport/ui/RmlSdlRenderInterface.h"
#include "dkrport/ui/RmlSdlSystemInterface.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

namespace dkrport {

struct FileDialogState {
    std::mutex mutex;
    bool complete = false;
    bool cancelled = false;
    std::string path;
    std::string error;
};

namespace {

std::string EscapeRml(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (const char character : input) {
        switch (character) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            default: output.push_back(character); break;
        }
    }
    return output;
}

std::string FileUrl(const std::filesystem::path& path) {
    std::string generic = PathToUtf8(std::filesystem::absolute(path));
    std::replace(generic.begin(), generic.end(), '\\', '/');
    std::ostringstream url;
#ifdef _WIN32
    url << "file:///";
#else
    url << "file://";
#endif
    for (const char rawCharacter : generic) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        const bool safe = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '/' || character == ':' ||
                          character == '-' || character == '_' || character == '.' || character == '~';
        if (safe) {
            url << static_cast<char>(character);
        } else {
            url << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(character) << std::nouppercase << std::dec;
        }
    }
    return url.str();
}

void SDLCALL RomDialogCallback(void* userdata, const char* const* fileList, int filter) {
    static_cast<void>(filter);
    std::unique_ptr<std::shared_ptr<FileDialogState>> holder(
        static_cast<std::shared_ptr<FileDialogState>*>(userdata));
    const std::shared_ptr<FileDialogState> state = *holder;

    std::lock_guard<std::mutex> lock(state->mutex);
    state->complete = true;
    if (!fileList) {
        const char* sdlError = SDL_GetError();
        state->error = (sdlError && *sdlError) ? sdlError : "The operating-system file picker failed without providing an error.";
    } else if (!*fileList) {
        state->cancelled = true;
    } else {
        state->path = *fileList;
    }
}

std::optional<input::Action> ActionFromId(const std::string& id) {
    for (int value = 0; value < static_cast<int>(input::Action::Count); ++value) {
        const auto action = static_cast<input::Action>(value);
        if (id == input::ActionId(action)) return action;
    }
    return std::nullopt;
}

std::string HexValue(std::uint64_t value, int width) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return output.str();
}

std::string BuildDiagnostics(const Application& application, const AppStatus& status) {
    std::ostringstream report;
    report << "DKR Port " << DKRPORT_VERSION_STRING << " (" << DKRPORT_BUILD_MILESTONE << ")\n";
    report << "Native launcher: SDL3 + RmlUi\n";
    report << "Portable mode: " << (status.portable ? "Yes" : "No") << "\n";
    report << "Data directory: " << PathToUtf8(application.Paths().dataRoot) << "\n";
    report << "Data directory ready: " << (status.dataDirectoryReady ? "Yes" : "No") << "\n";
    report << "Validated ROM: " << (status.hasValidatedRom ? "Yes" : "No") << "\n";
    report << "ROM source available: " << (status.romSourceAvailable ? "Yes" : "No") << "\n";
    if (status.hasValidatedRom) {
        report << "ROM: " << status.rom.displayName << "\n";
        report << "ROM byte order: " << status.rom.sourceOrderName << "\n";
        report << "ROM SHA-1: " << status.rom.sha1 << "\n";
    }
    report << "Local O2R manifest: " << (status.placeholderArchivePresent ? "Present" : "Missing") << "\n";
    report << "F3DDKR registry: " << (status.f3ddkrRegistryValid ? "Valid" : "Invalid") << "\n";
    report << "F3DDKR details: " << status.f3ddkrDetails << "\n";
    report << "Log file: " << PathToUtf8(application.Log().CurrentLogPath()) << "\n";
    return report.str();
}

} // namespace

NativeLauncher::NativeLauncher(Application& application)
    : m_application(application), m_window(nullptr), m_renderer(nullptr), m_context(nullptr), m_document(nullptr),
      m_fileDialogPending(false), m_romWorkPending(false), m_running(false), m_sdlInitialised(false),
      m_rmlInitialised(false), m_messageVisible(false), m_processingVisible(false), m_gameRunning(false),
      m_gameProcess(nullptr), m_lastGameTickNs(0U), m_lastGameUiUpdateNs(0U), m_activePage("page-main") {}

NativeLauncher::~NativeLauncher() {
    Shutdown();
}

int NativeLauncher::Run() {
    std::string error;
    if (!InitialiseWindow(error)) {
        m_application.Log().Error(error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DKR Port could not start", error.c_str(), nullptr);
        Shutdown();
        return 1;
    }
    if (!InitialiseUi(error)) {
        m_application.Log().Error(error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DKR Port UI could not start", error.c_str(), m_window);
        Shutdown();
        return 1;
    }

    m_running = true;
    while (m_running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) HandleEvent(event);
        PollBackgroundWork();
        UpdateGameRuntime();
        m_context->Update();
        RenderFrame();
        SDL_Delay(4U);
    }

    Shutdown();
    return 0;
}

bool NativeLauncher::InitialiseWindow(std::string& error) {
    SDL_SetAppMetadata("DKR Port", DKRPORT_VERSION_STRING, "org.dkrport.launcher");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        error = std::string("SDL could not initialise: ") + SDL_GetError();
        return false;
    }
    m_sdlInitialised = true;

    const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!SDL_CreateWindowAndRenderer("DKR Port — Native Launcher", 1600, 900, flags, &m_window, &m_renderer)) {
        error = std::string("SDL could not create the native launcher window: ") + SDL_GetError();
        return false;
    }
    SDL_SetWindowMinimumSize(m_window, 1280, 720);
    SDL_SetRenderVSync(m_renderer, 1);

    m_inputManager = std::make_unique<input::InputManager>(
        m_application.Log(), m_application.Paths().configDirectory / "controls.json");
    if (!m_inputManager->Initialise(error)) {
        error = "The input system could not initialise: " + error;
        return false;
    }
    m_bootSession = std::make_unique<game::BootSession>(m_application);
    m_application.Log().Info(std::string("SDL renderer: ") +
                             (SDL_GetRendererName(m_renderer) ? SDL_GetRendererName(m_renderer) : "unknown"));
    return true;
}

bool NativeLauncher::InitialiseUi(std::string& error) {
    m_systemInterface = std::make_unique<RmlSdlSystemInterface>(m_window, m_application.Log());
    m_renderInterface = std::make_unique<RmlSdlRenderInterface>(m_renderer);
    Rml::SetSystemInterface(m_systemInterface.get());
    Rml::SetRenderInterface(m_renderInterface.get());

    if (!Rml::Initialise()) {
        error = "RmlUi failed to initialise.";
        return false;
    }
    m_rmlInitialised = true;
    if (!LoadSystemFont(error)) return false;

    int width = 0;
    int height = 0;
    if (!SDL_GetRenderOutputSize(m_renderer, &width, &height) || width <= 0 || height <= 0) {
        error = std::string("Could not determine the launcher render size: ") + SDL_GetError();
        return false;
    }

    m_context = Rml::CreateContext("main", {width, height});
    if (!m_context) {
        error = "RmlUi could not create the main launcher context.";
        return false;
    }
    m_context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(m_window));

    m_document = m_context->LoadDocument(AssetPath("launcher.rml"));
    if (!m_document) {
        error = "The native launcher layout could not be loaded from assets/ui/launcher.rml.";
        return false;
    }

    BindAction("action-select-rom", "select-rom");
    BindAction("action-game", "game");
    BindAction("action-controls", "controls");
    BindAction("action-diagnostics", "diagnostics");
    BindAction("action-settings", "settings");
    BindAction("action-exit", "exit");
    BindAction("diagnostics-back", "main");
    BindAction("diagnostics-run", "self-test");
    BindAction("diagnostics-renderer", "renderer-test");
    BindAction("diagnostics-copy", "copy-diagnostics");
    BindAction("diagnostics-logs", "open-logs");
    BindAction("settings-back", "main");
    BindAction("settings-open-data", "open-data");
    BindAction("settings-reset", "reset-data");
    BindAction("controls-back", "controls-back");
    BindAction("controls-reset", "controls-reset");
    BindAction("controls-deadzone-minus", "deadzone-minus");
    BindAction("controls-deadzone-plus", "deadzone-plus");
    BindAction("controls-stick", "controls-stick");
    BindAction("controls-invert-x", "invert-x");
    BindAction("controls-invert-y", "invert-y");
    BindAction("game-return", "game-return");
    BindAction("game-controls", "game-controls");
    BindAction("message-close", "close-message");
    BuildControlsPage();

    m_document->Show();
    SetText("footer-version", DKRPORT_VERSION_STRING);
    RefreshStatus();
    RefreshControlsPage();
    ShowPage("page-main");
    FocusFirstAction();
    return true;
}

void NativeLauncher::Shutdown() {
    if (m_romWorkPending && m_romFuture.valid()) {
        m_romFuture.wait();
        static_cast<void>(m_romFuture.get());
        m_romWorkPending = false;
    }

    if (m_gameProcess) {
        // Destroying the SDL handle does not terminate the game. If the
        // launcher is closed externally, let the runtime finish normally so
        // an in-progress EEPROM write is never interrupted.
        SDL_DestroyProcess(m_gameProcess);
        m_gameProcess = nullptr;
    }
    if (m_bootSession) m_bootSession->Stop();
    m_gameRunning = false;
    if (m_inputManager) {
        m_inputManager->Shutdown();
        m_inputManager.reset();
    }
    m_bootSession.reset();
    if (m_context) {
        // Keep event listeners alive until the context and its documents have detached them.
        Rml::RemoveContext("main");
        m_context = nullptr;
        m_document = nullptr;
    }
    m_listeners.clear();
    if (m_rmlInitialised) {
        Rml::Shutdown();
        m_rmlInitialised = false;
    }
    Rml::SetRenderInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
    m_renderInterface.reset();
    m_systemInterface.reset();
    m_fontData.clear();

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_sdlInitialised) {
        SDL_Quit();
        m_sdlInitialised = false;
    }
}

void NativeLauncher::HandleEvent(SDL_Event& event) {
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        m_running = false;
        return;
    }

    const bool deviceChanged = event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED;
    if (m_inputManager && m_inputManager->HandleEvent(event)) return;
    if (deviceChanged) {
        RefreshControlsPage();
        RefreshStatus();
    }

    if (m_gameRunning && m_activePage == "page-game") {
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.key == SDLK_ESCAPE) { StopGame(); return; }
            if (event.key.key == SDLK_F1) { RefreshControlsPage(); ShowPage("page-controls"); return; }
            return;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) StopGame();
            return;
        }
        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) return;
    }

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (event.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: MoveFocus(-1); return;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: MoveFocus(1); return;
            case SDL_GAMEPAD_BUTTON_SOUTH: ActivateFocused(); return;
            case SDL_GAMEPAD_BUTTON_EAST:
                if (m_messageVisible) HideMessage();
                else if (!m_processingVisible && m_activePage != "page-main") ShowPage(m_gameRunning ? "page-game" : "page-main");
                return;
            default: break;
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_UP || event.key.key == SDLK_LEFT) {
            MoveFocus(-1);
            return;
        }
        if (event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT) {
            MoveFocus(1);
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE) {
            ActivateFocused();
            return;
        }
        if (event.key.key == SDLK_ESCAPE) {
            if (m_inputManager && m_inputManager->CaptureActive()) {
                m_inputManager->CancelCapture();
                return;
            }
            if (m_gameRunning && m_activePage == "page-game") {
                StopGame();
                return;
            }
            if (m_messageVisible) {
                HideMessage();
                return;
            }
            if (!m_processingVisible && m_activePage != "page-main") {
                ShowPage(m_gameRunning ? "page-game" : "page-main");
                return;
            }
        }
    }

    RmlSdlSystemInterface::ProcessEvent(m_context, m_window, event);
}

void NativeLauncher::HandleAction(const std::string& action) {
    if (m_processingVisible) return;
    if (action == "select-rom") SelectRom();
    else if (action == "game") StartGame();
    else if (action == "controls" || action == "game-controls") {
        ShowMessage("GAME CONTROLS",
            "Keyboard\n"
            "WASD: analogue stick    Arrow keys: D-pad\n"
            "Space: A    Shift: B    Z: Z trigger    Enter: Start\n"
            "IJKL: C buttons    Q/E: L/R\n\n"
            "Gamepad\n"
            "Left stick: steering    South: A    West/East: B\n"
            "Left trigger: Z    Shoulders: L/R    Right stick: C buttons",
            false);
    }
    else if (action == "game-return") StopGame();
    else if (action == "controls-back") ShowPage(m_gameRunning ? "page-game" : "page-main");
    else if (action == "controls-reset") {
        std::string error;
        if (m_inputManager && m_inputManager->ResetDefaults(error)) { RefreshControlsPage(); ShowMessage("CONTROLS RESET", "The default keyboard and controller mappings have been restored.", false); }
        else ShowMessage("CONTROLS COULD NOT RESET", error, true);
    } else if (action == "deadzone-minus" || action == "deadzone-plus") {
        std::string error;
        const float step = action == "deadzone-plus" ? 0.02F : -0.02F;
        if (m_inputManager && m_inputManager->SetDeadzone(m_inputManager->Config().deadzone + step, error)) RefreshControlsPage();
        else if (!error.empty()) ShowMessage("DEADZONE COULD NOT SAVE", error, true);
    } else if (action == "controls-stick") {
        std::string error;
        if (m_inputManager && m_inputManager->ToggleAnalogueStick(error)) RefreshControlsPage();
        else ShowMessage("CONTROL SETTING COULD NOT SAVE", error, true);
    } else if (action == "invert-x" || action == "invert-y") {
        std::string error;
        const bool saved = m_inputManager && (action == "invert-x" ? m_inputManager->ToggleInvertX(error) : m_inputManager->ToggleInvertY(error));
        if (saved) RefreshControlsPage(); else ShowMessage("CONTROL SETTING COULD NOT SAVE", error, true);
    } else if (action.rfind("bind:", 0U) == 0U) {
        const std::size_t first = action.find(':');
        const std::size_t second = action.find(':', first + 1U);
        if (second != std::string::npos) BeginBindingCapture(action.substr(first + 1U, second - first - 1U), action.substr(second + 1U));
    }
    else if (action == "diagnostics") ShowPage("page-diagnostics");
    else if (action == "settings") ShowPage("page-settings");
    else if (action == "main") ShowPage("page-main");
    else if (action == "exit") m_running = false;
    else if (action == "self-test") RunSelfTest();
    else if (action == "renderer-test") {
        const std::filesystem::path output = m_application.Paths().screenshotsDirectory / "f3ddkr-structural-test.svg";
        const int code = m_application.RunRendererTest(PathToUtf8(output));
        ShowMessage(code == 0 ? "RENDERER TEST WRITTEN" : "RENDERER TEST FAILED",
                    code == 0 ? "The asset-free F3DDKR structural test was written to:\n" + PathToUtf8(output)
                              : "The structural test could not be written. Open the log folder for details.",
                    code != 0);
    } else if (action == "copy-diagnostics") {
        const std::string report = BuildDiagnostics(m_application, m_application.Status());
        if (SDL_SetClipboardText(report.c_str())) ShowMessage("DIAGNOSTICS COPIED", "The diagnostic summary is now on your clipboard.", false);
        else ShowMessage("COPY FAILED", std::string("SDL could not copy diagnostics: ") + SDL_GetError(), true);
    } else if (action == "open-logs") OpenPath("logs");
    else if (action == "open-data") OpenPath("data");
    else if (action == "reset-data") ResetGameData();
    else if (action == "close-message") HideMessage();
}

void NativeLauncher::SelectRom() {
    if (m_fileDialogPending) return;

    static constexpr SDL_DialogFileFilter filters[] = {
        {"Nintendo 64 ROM", "z64;v64;n64"},
    };

    m_fileDialogState = std::make_shared<FileDialogState>();
    auto* callbackState = new std::shared_ptr<FileDialogState>(m_fileDialogState);
    m_fileDialogPending = true;
    SDL_ShowOpenFileDialog(RomDialogCallback, callbackState, m_window, filters, 1, nullptr, false);
}

void NativeLauncher::StartRomValidation(const std::string& path) {
    if (m_romWorkPending) return;
    m_processingVisible = true;
    SetVisible("processing-overlay", true);
    m_romFuture = std::async(std::launch::async, [this, path]() { return m_application.ValidateAndPersistRom(path); });
    m_romWorkPending = true;
}

void NativeLauncher::PollBackgroundWork() {
    if (m_fileDialogPending && m_fileDialogState) {
        bool complete = false;
        bool cancelled = false;
        std::string selectedPath;
        std::string dialogError;
        {
            std::lock_guard<std::mutex> lock(m_fileDialogState->mutex);
            complete = m_fileDialogState->complete;
            if (complete) {
                cancelled = m_fileDialogState->cancelled;
                selectedPath = m_fileDialogState->path;
                dialogError = m_fileDialogState->error;
            }
        }
        if (complete) {
            m_fileDialogPending = false;
            m_fileDialogState.reset();
            if (!dialogError.empty()) {
                ShowMessage("ROM SELECTOR FAILED", dialogError, true);
            } else if (!cancelled && !selectedPath.empty()) {
                StartRomValidation(selectedPath);
            }
        }
    }

    if (m_inputManager) {
        if (const auto captureResult = m_inputManager->TakeCaptureResult()) {
            SetVisible("capture-overlay", false);
            RefreshControlsPage();
            if (!captureResult->error.empty()) {
                ShowMessage("CONTROL MAPPING COULD NOT SAVE", captureResult->error, true);
            } else if (captureResult->cancelled) {
                m_application.Log().Info("Control mapping capture cancelled.");
            } else if (captureResult->completed) {
                ShowMessage("CONTROL MAPPED", std::string(input::ActionLabel(captureResult->action)) +
                    " is now mapped to " + captureResult->binding + ".", false);
            }
        }
    }

    if (!m_romWorkPending || !m_romFuture.valid()) return;
    if (m_romFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;

    RomOperationResult result = m_romFuture.get();
    m_romWorkPending = false;
    m_processingVisible = false;
    SetVisible("processing-overlay", false);
    RefreshStatus();

    if (result.persisted) {
        ShowMessage("ROM VALIDATED",
                    result.rom.displayName + " was recognised successfully. The local O2R currently contains only a manifest and legal notice; no ROM data was copied.",
                    false);
    } else {
        std::string details = result.error.empty() ? "The selected file could not be validated." : result.error;
        if (!result.rom.sha1.empty()) details += "\n\nCalculated SHA-1: " + result.rom.sha1;
        ShowMessage("UNSUPPORTED ROM", details, true);
    }
}

void NativeLauncher::UpdateGameRuntime() {
    if (!m_gameRunning || !m_gameProcess) return;

    int exitCode = 0;
    if (!SDL_WaitProcess(m_gameProcess, false, &exitCode)) return;

    SDL_DestroyProcess(m_gameProcess);
    m_gameProcess = nullptr;
    m_gameRunning = false;
    SDL_SetWindowTitle(m_window, "DKR Port — Native Launcher");
    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);
    RefreshStatus();
    ShowPage("page-main");

    if (exitCode == 0) {
        m_application.Log().Info("DKR runtime exited normally.");
    } else {
        const std::string details = "The game runtime exited with code " + std::to_string(exitCode) +
            ". Open the log folder and include runtime.log when reporting the problem.";
        m_application.Log().Error(details);
        ShowMessage("GAME RUNTIME STOPPED", details, true);
    }
}

void NativeLauncher::StartGame() {
    const AppStatus status = m_application.Status();
    if (!status.hasValidatedRom) {
        ShowMessage("ROM REQUIRED", "Select and validate a supported Diddy Kong Racing ROM before starting the game host.", true);
        return;
    }
    if (!status.romSourceAvailable) {
        ShowMessage("ROM FILE NOT FOUND", "The ROM was validated previously, but its original file is no longer available. Select the ROM again to reconnect it.", true);
        return;
    }
    if (m_gameProcess) return;

#ifdef _WIN32
    const std::filesystem::path runtimePath = m_application.Paths().executableDirectory / "DKRPortGame.exe";
#else
    const std::filesystem::path runtimePath = m_application.Paths().executableDirectory / "DKRPortGame";
#endif
    if (!std::filesystem::is_regular_file(runtimePath)) {
        ShowMessage("GAME RUNTIME NOT FOUND",
            "The launcher could not find the bundled game runtime at:\n\n" + PathToUtf8(runtimePath) +
            "\n\nReinstall the complete DKR Port release package.", true);
        return;
    }

    const std::filesystem::path runtimeData = m_application.Paths().dataRoot / "runtime";
    std::error_code directoryError;
    std::filesystem::create_directories(runtimeData, directoryError);
    if (directoryError) {
        ShowMessage("GAME DATA DIRECTORY FAILED",
            "The launcher could not prepare the runtime data directory:\n\n" + PathToUtf8(runtimeData) +
            "\n\n" + directoryError.message(), true);
        return;
    }

    const std::array<std::string, 3> argumentStorage = {
        PathToUtf8(runtimePath),
        PathToUtf8(m_application.ValidatedRomPath()),
        PathToUtf8(runtimeData),
    };
    const std::array<const char*, 4> arguments = {
        argumentStorage[0].c_str(), argumentStorage[1].c_str(), argumentStorage[2].c_str(), nullptr,
    };

    const std::filesystem::path runtimeLogPath = m_application.Paths().logsDirectory / "runtime.log";
    SDL_IOStream* runtimeLog = SDL_IOFromFile(PathToUtf8(runtimeLogPath).c_str(), "ab");
    const SDL_PropertiesID processProperties = SDL_CreateProperties();
    if (runtimeLog && processProperties != 0) {
        SDL_SetPointerProperty(processProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                               const_cast<const char**>(arguments.data()));
        SDL_SetStringProperty(processProperties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                              PathToUtf8(m_application.Paths().executableDirectory).c_str());
        SDL_SetNumberProperty(processProperties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                              SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(processProperties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, runtimeLog);
        SDL_SetBooleanProperty(processProperties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        m_gameProcess = SDL_CreateProcessWithProperties(processProperties);
    } else {
        m_gameProcess = SDL_CreateProcess(arguments.data(), false);
    }
    if (processProperties != 0) SDL_DestroyProperties(processProperties);
    if (runtimeLog) SDL_CloseIO(runtimeLog);

    if (!m_gameProcess) {
        ShowMessage("GAME COULD NOT START",
                    std::string("SDL could not launch the bundled runtime: ") + SDL_GetError(), true);
        return;
    }

    m_gameRunning = true;
    m_application.Log().Info("Started DKR runtime: " + PathToUtf8(runtimePath));
    SDL_SetWindowTitle(m_window, "DKR Port — Game Running");
    SDL_HideWindow(m_window);
}

void NativeLauncher::StopGame() {
    if (m_gameProcess) {
        if (!SDL_KillProcess(m_gameProcess, false)) {
            ShowMessage("GAME COULD NOT CLOSE",
                        std::string("SDL could not request a clean runtime shutdown: ") + SDL_GetError(), true);
        }
        return;
    }
    m_gameRunning = false;
    SDL_SetWindowTitle(m_window, "DKR Port — Native Launcher");
    ShowPage("page-main");
}

void NativeLauncher::RefreshGamePage() {
    if (!m_bootSession || !m_inputManager) return;
    const game::BootSnapshot snapshot = m_bootSession->Snapshot();
    SetText("game-stage", game::BootStageName(snapshot.stage));
    SetText("game-status", snapshot.status);
    SetText("game-frame", std::to_string(snapshot.frame));
    SetText("game-rom-size", std::to_string(snapshot.romBytes / (1024U * 1024U)) + " MiB");
    SetText("game-rdram-size", std::to_string(snapshot.rdramBytes / (1024U * 1024U)) + " MiB");
    SetText("game-entry", HexValue(snapshot.entryPoint, 8));
    SetText("game-crc", HexValue(snapshot.crc1, 8) + " / " + HexValue(snapshot.crc2, 8));
    SetText("game-internal-name", snapshot.internalName.empty() ? "DIDDY KONG RACING" : snapshot.internalName);
    SetText("game-controller", m_inputManager->PrimaryGamepadName());
    SetText("game-buttons", HexValue(snapshot.controller.buttons, 4));
    SetText("game-stick-x", std::to_string(static_cast<int>(snapshot.controller.stickX)));
    SetText("game-stick-y", std::to_string(static_cast<int>(snapshot.controller.stickY)));
    const float x = (static_cast<float>(snapshot.controller.stickX) / 80.0F) * 42.0F;
    const float y = (-static_cast<float>(snapshot.controller.stickY) / 80.0F) * 42.0F;
    if (Rml::Element* dot = m_document->GetElementById("game-stick-dot")) {
        dot->SetProperty("left", std::to_string(x + 48.0F) + "dp");
        dot->SetProperty("top", std::to_string(y + 48.0F) + "dp");
    }
}

void NativeLauncher::BuildControlsPage() {
    if (!m_document) return;
    Rml::Element* container = m_document->GetElementById("controls-list");
    if (!container) return;

    std::string rows;
    for (int value = 0; value < static_cast<int>(input::Action::Count); ++value) {
        const auto action = static_cast<input::Action>(value);
        const std::string id = input::ActionId(action);
        rows += "<div class=\"control-row\"><div class=\"control-action-name\">";
        rows += EscapeRml(input::ActionLabel(action));
        rows += "</div><button id=\"bind-" + id + "-keyboard\" class=\"binding-button\"><span class=\"binding-type\">KEYBOARD</span><span id=\"value-" + id + "-keyboard\" class=\"binding-value\">—</span></button>";
        if (action < input::Action::StickUp) {
            rows += "<button id=\"bind-" + id + "-gamepad\" class=\"binding-button\"><span class=\"binding-type\">CONTROLLER</span><span id=\"value-" + id + "-gamepad\" class=\"binding-value\">—</span></button>";
        } else {
            rows += "<div class=\"binding-readonly\"><span class=\"binding-type\">CONTROLLER</span><span class=\"binding-value\">ANALOGUE STICK</span></div>";
        }
        rows += "</div>";
    }
    container->SetInnerRML(rows);

    for (int value = 0; value < static_cast<int>(input::Action::Count); ++value) {
        const auto action = static_cast<input::Action>(value);
        const std::string id = input::ActionId(action);
        BindAction("bind-" + id + "-keyboard", "bind:" + id + ":keyboard");
        if (action < input::Action::StickUp) BindAction("bind-" + id + "-gamepad", "bind:" + id + ":gamepad");
    }
}

void NativeLauncher::RefreshControlsPage() {
    if (!m_inputManager) return;
    const input::InputConfig& config = m_inputManager->Config();
    for (int value = 0; value < static_cast<int>(input::Action::Count); ++value) {
        const auto action = static_cast<input::Action>(value);
        const std::string id = input::ActionId(action);
        const input::ActionBinding& binding = config.actions[static_cast<std::size_t>(action)];
        SetText("value-" + id + "-keyboard", binding.keyboard);
        if (action < input::Action::StickUp) SetText("value-" + id + "-gamepad", binding.gamepad);
    }
    SetText("controls-controller-name", m_inputManager->PrimaryGamepadName());
    SetText("controls-deadzone-value", std::to_string(static_cast<int>(std::lround(config.deadzone * 100.0F))) + "%");
    SetText("controls-stick-value", config.analogueStick + " stick");
    SetText("controls-invert-x-value", config.invertX ? "ON" : "OFF");
    SetText("controls-invert-y-value", config.invertY ? "ON" : "OFF");
}

void NativeLauncher::BeginBindingCapture(const std::string& actionId, const std::string& slotId) {
    if (!m_inputManager) return;
    const std::optional<input::Action> action = ActionFromId(actionId);
    if (!action) {
        ShowMessage("UNKNOWN CONTROL", "The selected control action could not be identified.", true);
        return;
    }
    const input::BindingSlot slot = slotId == "gamepad" ? input::BindingSlot::Gamepad : input::BindingSlot::Keyboard;
    m_inputManager->BeginCapture(*action, slot);
    SetText("capture-title", input::ActionLabel(*action));
    SetText("capture-instruction", slot == input::BindingSlot::Keyboard
        ? "Press a keyboard key. Press Backspace to clear the binding or Escape to cancel."
        : "Press a controller button or move an axis fully. Press Escape to cancel.");
    SetVisible("capture-overlay", true);
}

void NativeLauncher::RunSelfTest() {
    const SelfTestSummary summary = m_application.ExecuteSelfTests();
    SetText("self-test-summary", std::to_string(summary.passed) + " of " + std::to_string(summary.total) +
                                     " checks passed during this launch.");
    std::string rows;
    for (const auto& [name, passed] : summary.checks) {
        rows += "<div class=\"self-test-row ";
        rows += passed ? "test-pass\">PASS" : "test-fail\">FAIL";
        rows += " <span>" + EscapeRml(name) + "</span></div>";
    }
    if (Rml::Element* list = m_document->GetElementById("self-test-list")) list->SetInnerRML(rows);
    ShowMessage(summary.Passed() ? "SELF-TEST PASSED" : "SELF-TEST FAILED",
                std::to_string(summary.passed) + " of " + std::to_string(summary.total) +
                    " checks passed. Full details remain visible on the Diagnostics page.",
                !summary.Passed());
}

void NativeLauncher::ResetGameData() {
    std::string error;
    if (!m_application.ResetGeneratedData(error)) {
        ShowMessage("RESET FAILED", error, true);
        return;
    }
    RefreshStatus();
    ShowMessage("GAME DATA RESET", "The generated manifest and local O2R were removed. You can now select a different supported ROM.", false);
}

void NativeLauncher::OpenPath(const std::string& which) {
    const std::filesystem::path path = which == "logs" ? m_application.Paths().logsDirectory : m_application.Paths().dataRoot;
    if (!SDL_OpenURL(FileUrl(path).c_str())) {
        ShowMessage("FOLDER COULD NOT OPEN", std::string("SDL could not open the folder: ") + SDL_GetError() + "\n\n" + PathToUtf8(path), true);
    }
}

void NativeLauncher::ShowPage(const std::string& pageId) {
    SetVisible("page-main", pageId == "page-main");
    SetVisible("page-diagnostics", pageId == "page-diagnostics");
    SetVisible("page-settings", pageId == "page-settings");
    SetVisible("page-controls", pageId == "page-controls");
    SetVisible("page-game", pageId == "page-game");
    m_activePage = pageId;
    FocusFirstAction();
}

void NativeLauncher::ShowMessage(const std::string& title, const std::string& message, bool error) {
    SetText("message-title", title);
    SetText("message-body", message);
    SetText("message-type", error ? "ERROR" : "INFORMATION");
    SetClass("message-modal", "message-error", error);
    m_messageVisible = true;
    SetVisible("message-overlay", true);
    if (Rml::Element* close = m_document->GetElementById("message-close")) close->Focus(true);
}

void NativeLauncher::HideMessage() {
    m_messageVisible = false;
    SetVisible("message-overlay", false);
    FocusFirstAction();
}

void NativeLauncher::RefreshStatus() {
    const AppStatus status = m_application.Status();
    const bool bootReady = status.hasValidatedRom && status.romSourceAvailable;
    SetText("diag-data", status.dataDirectoryReady ? "READY" : "ERROR");
    SetText("diag-rom", status.hasValidatedRom ? "VALIDATED" : "NOT FOUND");
    SetText("diag-source", status.romSourceAvailable ? "CONNECTED" : "NOT FOUND");
    SetText("diag-o2r", status.placeholderArchivePresent ? "PRESENT" : "NOT FOUND");
    SetText("diag-controller", m_inputManager ? m_inputManager->PrimaryGamepadName() : "NOT INITIALISED");
    SetText("diag-f3ddkr", status.f3ddkrRegistryValid ? "VALID" : "INVALID");
    SetText("diag-portable", status.portable ? "PORTABLE" : "USER DATA");
    SetText("setting-portable", status.portable ? "ENABLED" : "DISABLED");
    SetText("setting-data-path", PathToUtf8(m_application.Paths().dataRoot));

    SetClass("action-game", "action-disabled", !bootReady);
    SetClass("action-game", "action-ready", bootReady);
    SetText("game-action-subtitle", bootReady
        ? "Launch the complete native game runtime"
        : status.hasValidatedRom
            ? "Select the ROM once more to reconnect its source path."
            : "Validate a supported ROM before starting the game.");
    SetText("game-action-lock", bootReady ? "READY" : "LOCKED");

    if (status.hasValidatedRom) {
        SetText("header-status", status.romSourceAvailable ? "ROM READY" : "ROM RECONNECT REQUIRED");
        SetClass("header-status", "status-good", status.romSourceAvailable);
        SetClass("header-status", "status-neutral", !status.romSourceAvailable);
        SetClass("rom-status-card", "status-card-good", status.romSourceAvailable);
        SetText("rom-status-icon", status.romSourceAvailable ? "OK" : "!");
        SetClass("rom-status-icon", "status-orb-good", status.romSourceAvailable);
        SetClass("rom-status-icon", "status-orb-neutral", !status.romSourceAvailable);
        SetText("rom-status-kicker", status.romSourceAvailable ? "SUPPORTED GAME DATA" : "ROM SOURCE REQUIRED");
        SetText("rom-status-title", status.rom.displayName.empty() ? "Diddy Kong Racing US 1.0" : status.rom.displayName);
        SetText("rom-status-description", status.romSourceAvailable
            ? "The ROM is validated and connected. DKR Port is ready to play."
            : "The validation manifest is present, but this version needs the original ROM path. Select the same ROM once to reconnect it.");
        SetText("rom-detail-version", status.rom.region + " " + status.rom.revision);
        SetText("rom-detail-format", status.rom.sourceOrderName);
        SetText("rom-detail-sha", status.rom.sha1);
        SetVisible("rom-detail-grid", true);
        SetText("select-rom-label", status.romSourceAvailable ? "CHANGE ROM" : "RECONNECT ROM");
    } else {
        SetText("header-status", "NO ROM LOADED");
        SetClass("header-status", "status-good", false);
        SetClass("header-status", "status-neutral", true);
        SetClass("rom-status-card", "status-card-good", false);
        SetText("rom-status-icon", "?");
        SetClass("rom-status-icon", "status-orb-good", false);
        SetClass("rom-status-icon", "status-orb-neutral", true);
        SetText("rom-status-kicker", "GAME DATA REQUIRED");
        SetText("rom-status-title", "Select a supported ROM");
        SetText("rom-status-description", "Diddy Kong Racing US 1.0 is supported in .z64, .v64 and .n64 formats.");
        SetVisible("rom-detail-grid", false);
        SetText("select-rom-label", "SELECT ROM");
    }
}

void NativeLauncher::SetText(const std::string& elementId, const std::string& text) {
    if (!m_document) return;
    if (Rml::Element* element = m_document->GetElementById(elementId)) element->SetInnerRML(EscapeRml(text));
}

void NativeLauncher::SetClass(const std::string& elementId, const std::string& className, bool enabled) {
    if (!m_document) return;
    if (Rml::Element* element = m_document->GetElementById(elementId)) element->SetClass(className, enabled);
}

void NativeLauncher::SetVisible(const std::string& elementId, bool visible) {
    SetClass(elementId, "hidden", !visible);
}

void NativeLauncher::BindAction(const std::string& elementId, const std::string& action) {
    if (!m_document) return;
    Rml::Element* element = m_document->GetElementById(elementId);
    if (!element) {
        m_application.Log().Warning("Native launcher element was not found: " + elementId);
        return;
    }
    auto listener = std::make_unique<RmlEventListener>(action, [this](const std::string& selected) {
        HandleAction(selected);
    });
    element->AddEventListener("click", listener.get());
    m_listeners.push_back(std::move(listener));
}

void NativeLauncher::FocusFirstAction() {
    if (!m_document || m_messageVisible || m_processingVisible) return;
    std::string elementId = "action-select-rom";
    if (m_activePage == "page-diagnostics") elementId = "diagnostics-run";
    else if (m_activePage == "page-settings") elementId = "settings-open-data";
    else if (m_activePage == "page-controls") elementId = "bind-a-keyboard";
    else if (m_activePage == "page-game") elementId = "game-controls";
    if (Rml::Element* element = m_document->GetElementById(elementId)) element->Focus(true);
}

void NativeLauncher::MoveFocus(int direction) {
    if (!m_document || m_processingVisible) return;
    const std::vector<std::string> actions = ActiveActionIds();
    if (actions.empty()) return;

    int currentIndex = -1;
    if (m_context) {
        if (Rml::Element* focused = m_context->GetFocusElement()) {
            const std::string focusedId = focused->GetId();
            const auto found = std::find(actions.begin(), actions.end(), focusedId);
            if (found != actions.end()) currentIndex = static_cast<int>(std::distance(actions.begin(), found));
        }
    }

    const int count = static_cast<int>(actions.size());
    int nextIndex = currentIndex < 0 ? 0 : (currentIndex + direction) % count;
    if (nextIndex < 0) nextIndex += count;
    if (Rml::Element* element = m_document->GetElementById(actions[static_cast<std::size_t>(nextIndex)])) {
        element->Focus(true);
        element->ScrollIntoView(false);
    }
}

void NativeLauncher::ActivateFocused() {
    if (!m_document || m_processingVisible) return;
    if (m_messageVisible) {
        if (Rml::Element* close = m_document->GetElementById("message-close")) close->Click();
        return;
    }
    if (m_context) {
        if (Rml::Element* focused = m_context->GetFocusElement()) {
            focused->Click();
            return;
        }
    }
    FocusFirstAction();
}

std::vector<std::string> NativeLauncher::ActiveActionIds() const {
    if (m_messageVisible) return {"message-close"};
    if (m_activePage == "page-diagnostics") {
        return {"diagnostics-run", "diagnostics-renderer", "diagnostics-copy", "diagnostics-logs", "diagnostics-back"};
    }
    if (m_activePage == "page-settings") {
        return {"settings-open-data", "settings-reset", "settings-back"};
    }
    if (m_activePage == "page-game") return {"game-controls", "game-return"};
    if (m_activePage == "page-controls") {
        std::vector<std::string> ids;
        for (int value = 0; value < static_cast<int>(input::Action::Count); ++value) {
            const auto action = static_cast<input::Action>(value);
            ids.push_back(std::string("bind-") + input::ActionId(action) + "-keyboard");
            if (action < input::Action::StickUp) ids.push_back(std::string("bind-") + input::ActionId(action) + "-gamepad");
        }
        ids.insert(ids.end(), {"controls-deadzone-minus", "controls-deadzone-plus", "controls-stick",
                               "controls-invert-x", "controls-invert-y", "controls-reset", "controls-back"});
        return ids;
    }
    return {"action-select-rom", "action-game", "action-controls", "action-diagnostics", "action-settings", "action-exit"};
}

bool NativeLauncher::LoadSystemFont(std::string& error) {
    const std::string fontPath = FindSystemFont();
    if (fontPath.empty()) {
        error = "No supported system font could be found. Install Segoe UI, Arial, DejaVu Sans, Liberation Sans or Helvetica.";
        return false;
    }
    if (!ReadBinaryFile(fontPath, m_fontData, error, 64ULL * 1024ULL * 1024ULL)) {
        error = "The system font could not be read: " + error;
        return false;
    }
    const Rml::Span<const Rml::byte> fontBytes(m_fontData.data(), m_fontData.size());
    if (!Rml::LoadFontFace(fontBytes, "DKRUI", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto)) {
        error = "RmlUi could not load the system font: " + fontPath;
        return false;
    }
    m_application.Log().Info("Native launcher font: " + fontPath);
    return true;
}

std::string NativeLauncher::FindSystemFont() const {
    const std::vector<std::filesystem::path> candidates = {
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
#elif __APPLE__
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return PathToUtf8(candidate);
    }
    return {};
}

std::string NativeLauncher::AssetPath(const std::string& relative) const {
    return PathToUtf8(m_application.Paths().executableDirectory / "assets" / "ui" / relative);
}

void NativeLauncher::RenderFrame() {
    SDL_SetRenderDrawColor(m_renderer, 7U, 16U, 25U, 255U);
    SDL_RenderClear(m_renderer);
    m_context->Render();
    SDL_RenderPresent(m_renderer);
}

} // namespace dkrport
