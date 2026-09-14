#include "legacy_mod_launch.hpp"
#include "legacy_track_catalog.hpp"
#include "../dkr_save_codec.hpp"
#include <algorithm>

namespace dkr::mods {
namespace {
void ordinary(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_directory(path))
        throw Error("The modded-save path must contain ordinary directories, not links.");
}
void child(const std::filesystem::path& path) {std::filesystem::create_directory(path);ordinary(path);}
void regular(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        throw Error("The Adventure save must be a regular file, not a link.");
}
}
std::shared_ptr<const PreparedModLaunch> prepare_mod_launch(
    const std::filesystem::path& config,const std::filesystem::path& rom,bool online,
    const LaunchProgress& progress,std::stop_token stop) {
    auto out=std::make_shared<PreparedModLaunch>();
    // No catalogue reads or asset allocation online. The accepted network
    // manifest, topology, saves and rollback system remain entirely stock.
    if(online)return out;
    const auto root=config/"mods"/"legacy";
    if(!TrackCatalog::has_enabled(root))return out;
    auto report=[&](const char* message){if(stop.stop_requested())throw Error("Game launch cancelled.");if(progress)progress(message);};
    report("Validating the original Game Pak");
    auto bytes=read_file(rom,MaxImage);canonicalize_rom(bytes);
    const auto stock=AssetBank::stock(std::move(bytes));
    report("Checking enabled character assets");
    auto characters=TrackCatalog::load_enabled_characters(root,stock);
    if(characters.size()>MaxActiveStageCharacters)throw Error("This beta supports two extra active characters. Disable other characters in Mods / Hacks before starting.");
    std::shared_ptr<const CharacterNamespace> names;
    if(!characters.empty())names=std::make_shared<const CharacterNamespace>(allocate_characters(stock,std::move(characters)));
    report("Checking enabled custom courses");
    auto tracks=TrackCatalog::load_enabled(root,stock);
    report("Preparing the isolated game asset session");
    auto session=std::make_shared<RuntimeSession>(stock,names);
    std::vector<std::string> identities;
    for(auto& track:tracks) {
        report("Preparing custom Track Select previews");
        identities.push_back("track:"+track.root.content_id+":"+track.bank->fingerprint());
        session->admit(std::move(track));
    }
    if(names)for(const auto& character:names->characters)identities.push_back("character:"+character.id);
    if(identities.empty())throw Error("Enabled mod content did not produce a playable library.");
    std::sort(identities.begin(),identities.end());
    std::string identity="dkr-offline-mod-session-1\n"+stock->fingerprint()+"\n";
    for(const auto& part:identities)identity+=part+"\n";
    out->fingerprint=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    out->save_subfolder=std::filesystem::path("mods")/out->fingerprint;
    out->save_path=config/"saves"/out->save_subfolder/"dkr.us.v77.bin";
    out->pak_directory=config/"saves"/out->save_subfolder/"paks";
    report("Preparing separate modded-session saves");
    ordinary(config);child(config/"saves");child(config/"saves"/"mods");child(out->save_path.parent_path());child(out->pak_directory);
    using namespace dkr::runtime::saves;
    if(!std::filesystem::exists(out->save_path)) {
        const auto original=config/"saves"/"dkr.us.v77.bin";
        auto seed=codec::blank_bytes();
        if(std::filesystem::exists(original)) {
            regular(original);seed=read_file(original,codec::kImageSize);
            if(!codec::validate(seed))throw Error("The single-player Adventure save is invalid. It was not changed or copied into a modded session.");
        }
        // Exclusive creation, never replace an existing modded or stock save.
        write_new_file(out->save_path,seed);
    }
    regular(out->save_path);
    if(!codec::validate(read_file(out->save_path,codec::kImageSize)))throw Error("The separate modded-session save is invalid. Restore it before starting this mod set.");
    report("Custom mods ready");out->session=std::move(session);return out;
}
ModLaunch::~ModLaunch(){cancel();if(thread_.joinable())thread_.join();}
ModLaunchView ModLaunch::snapshot()const{std::lock_guard lock(mutex_);return state_;}
void ModLaunch::cancel(){
    if(thread_.joinable())thread_.request_stop();
    // Cancellation must win even if preparation completed just before the
    // button was pressed. A hidden/abandoned result can never auto-launch.
    std::lock_guard lock(mutex_);state_.modal=false;state_.prepared.reset();
}
void ModLaunch::abandon(){cancel();}
void ModLaunch::dismiss(){std::lock_guard lock(mutex_);if(!state_.busy){state_.modal=false;state_.prepared.reset();}}
void ModLaunch::report_error(std::string error){std::lock_guard lock(mutex_);if(!state_.busy){state_={};state_.modal=true;state_.error=std::move(error);}}
bool ModLaunch::start(std::filesystem::path config,std::filesystem::path rom,bool online) {
    {std::lock_guard lock(mutex_);if(state_.busy)return false;}
    if(thread_.joinable())thread_.join();
    {std::lock_guard lock(mutex_);state_={};state_.busy=true;state_.modal=true;state_.stage="Checking custom mods";}
    try {
        thread_=std::jthread([this,config=std::move(config),rom=std::move(rom),online](std::stop_token stop) {
            try {
                auto prepared=prepare_mod_launch(config,rom,online,[&](const char* stage){std::lock_guard lock(mutex_);state_.stage=stage;},stop);
                std::lock_guard lock(mutex_);
                if(state_.modal && !stop.stop_requested())state_.prepared=std::move(prepared);
                state_.busy=false;
            }catch(const std::exception& error){std::lock_guard lock(mutex_);state_.busy=false;state_.error=error.what();}
             catch(...){std::lock_guard lock(mutex_);state_.busy=false;state_.error="Custom mod preparation stopped safely after an unexpected error.";}
        });
    }catch(const std::exception& error){std::lock_guard lock(mutex_);state_.busy=false;state_.error=error.what();return false;}
    return true;
}
}
