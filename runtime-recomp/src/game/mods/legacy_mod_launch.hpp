#pragma once
#include "legacy_runtime_session.hpp"
#include <thread>
#include <functional>

namespace dkr::mods {
struct PreparedModLaunch {
    std::shared_ptr<RuntimeSession> session;
    std::filesystem::path save_subfolder,save_path,pak_directory;
    std::string fingerprint;
};
using LaunchProgress=std::function<void(const char*)>;
std::shared_ptr<const PreparedModLaunch> prepare_mod_launch(
    const std::filesystem::path& config,const std::filesystem::path& rom,bool online,
    const LaunchProgress& progress={},std::stop_token stop={});
struct ModLaunchView {
    bool busy=false,modal=false;
    std::string stage,error;
    std::shared_ptr<const PreparedModLaunch> prepared;
};
class ModLaunch {
public:
    ~ModLaunch();
    bool start(std::filesystem::path config,std::filesystem::path rom,bool online);
    void cancel();
    void abandon();
    void dismiss();
    void report_error(std::string error);
    ModLaunchView snapshot() const;
private:
    mutable std::mutex mutex_;
    ModLaunchView state_;
    std::jthread thread_;
};
}
