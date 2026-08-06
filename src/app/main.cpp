#include "dkrport/Version.h"
#include "dkrport/app/Application.h"
#if DKRPORT_HAS_NATIVE_UI
#include "dkrport/ui/NativeLauncher.h"
#include <SDL3/SDL_main.h>
#endif

#include <iostream>
#include <string>
#include <vector>

namespace {
void PrintUsage() {
    std::cout
        << "DKR-R " << DKRPORT_VERSION_STRING << " — " << DKRPORT_BUILD_MILESTONE << "\n\n"
        << "Usage:\n"
        << "  DKR-R [--portable]\n"
        << "  DKR-R --validate-rom <path> [--portable]\n"
        << "  DKR-R --self-test [--portable]\n"
        << "  DKR-R --headless-self-test [--portable]\n"
        << "  DKR-R --renderer-test [output.svg] [--portable]\n"
        << "  DKR-R --version\n"
        << "  DKR-R --help\n\n"
        << "The default action opens the native SDL3/RmlUi launcher. No browser or local web server is used.\n";
}
} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);

    bool forcePortable = false;
    for (const auto& argument : arguments) {
        if (argument == "--portable") forcePortable = true;
    }

    if (arguments.empty() || arguments[0] == "--portable") {
        dkrport::Application app(forcePortable);
        if (!app.Initialise()) return 1;
#if DKRPORT_HAS_NATIVE_UI
        dkrport::NativeLauncher launcher(app);
        return launcher.Run();
#else
        std::cerr << "This build was compiled without the native launcher. Configure with DKRPORT_BUILD_NATIVE_UI=ON.\n";
        return 1;
#endif
    }
    if (arguments[0] == "--version") {
        std::cout << DKRPORT_VERSION_STRING << " (" << DKRPORT_BUILD_MILESTONE << ")\n";
        return 0;
    }
    if (arguments[0] == "--help" || arguments[0] == "-h") {
        PrintUsage();
        return 0;
    }
    if (arguments[0] == "--validate-rom") {
        if (arguments.size() < 2U) {
            std::cerr << "--validate-rom requires a file path.\n";
            return 64;
        }
        dkrport::Application app(forcePortable);
        return app.ValidateRomCommand(arguments[1]);
    }
    if (arguments[0] == "--self-test" || arguments[0] == "--headless-self-test") {
        dkrport::Application app(forcePortable);
        return app.RunSelfTest(arguments[0] == "--headless-self-test");
    }
    if (arguments[0] == "--renderer-test") {
        std::string output;
        if (arguments.size() >= 2U && arguments[1].rfind("--", 0U) != 0U) output = arguments[1];
        dkrport::Application app(forcePortable);
        return app.RunRendererTest(output);
    }

    std::cerr << "Unknown option: " << arguments[0] << "\n\n";
    PrintUsage();
    return 64;
}
