#include "runtime_legacy_mods.hpp"
#include "game_payload.hpp"
#include "mods/legacy_runtime_assets.hpp"
#include "mods/legacy_runtime_io.hpp"
#include "mods/legacy_track_menu_adapter.hpp"
#include "mods/legacy_character_roster.hpp"
#include "mods/legacy_runtime_character.hpp"
#include "mods/legacy_character_menu_render.hpp"
#include "mods/legacy_character_stage.hpp"
#include "mods/legacy_character_presentation.hpp"
#if DKR_LEGACY_QUALIFICATION
#include "legacy_runtime_qualification.hpp"
#endif
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <bit>

extern "C" void osPiStartDma_recomp(std::uint8_t*,recomp_context*);
extern "C" void dkr_character_select_animation_fraction(std::uint8_t*,recomp_context*);
namespace dkr::runtime::legacy {
namespace {
std::atomic<std::shared_ptr<mods::RuntimeSession>> session;
std::atomic<std::shared_ptr<const mods::PreparedModLaunch>> launch;
struct MenuState {
    std::mutex mutex;
    mods::TrackMenuAdapter adapter;
    MenuState(std::vector<mods::Root> roots,bool races):adapter(std::move(roots),races){}
};
std::atomic<std::shared_ptr<MenuState>> menu;
struct CharacterState {
    std::mutex mutex;
    mods::CharacterRoster roster;
    std::unique_ptr<mods::CharacterMenuAdapter> selector;
    mods::CharacterStage stage;
    mods::CharacterPresentation presentation;
    std::vector<std::uint32_t> sound_banks;
    bool menu_active=false;
    explicit CharacterState(std::shared_ptr<const mods::CharacterNamespace> assets):roster(std::move(assets)){}
};
std::atomic<std::shared_ptr<CharacterState>> characters;
mods::CharacterPresentationCalls presentation_calls(const GamePayload& p) {
    return {p.asset_allocate,p.menu_texture_load,p.sound_bank_relocate,p.sound_bank_play,p.sound_parameter,p.sound_spatial_point};
}
std::mutex error_mutex;
std::string last_error;
[[noreturn]] void fail(const char* error) {
    {std::lock_guard lock(error_mutex);last_error=error;}
    std::fprintf(stderr,"[legacy][fatal] %s\n",error);
    // Never return a fake DMA success or throw an importer exception through
    // the guest scheduler. Its existing termination boundary owns teardown.
    ultramodern::quit();
    throw ultramodern::thread_terminated{};
}
}
void begin_session(std::shared_ptr<mods::RuntimeSession> value) {
    {std::lock_guard lock(error_mutex);last_error.clear();}
    launch.store(nullptr);characters.store(nullptr);menu.store(nullptr);session.store(std::move(value));
}
void begin_prepared(std::shared_ptr<const mods::PreparedModLaunch> prepared) {
    begin_session(prepared?prepared->session:nullptr);
    if(!prepared || !prepared->session)return;
    if(prepared->save_subfolder.empty() || prepared->save_path.empty() || prepared->pak_directory.empty())
        throw mods::Error("Modded runtime admission requires isolated save paths.");
    if(!prepared->session->tracks().empty())begin_track_menu(true);
    if(prepared->session->characters())begin_character_menu();
    launch.store(std::move(prepared));
}
std::shared_ptr<const mods::PreparedModLaunch> prepared_launch(){return launch.load();}
void begin_character_roster(const std::array<std::string,4>& selected) {
    auto active=session.load();if(!active || !active->characters())throw mods::Error("No characters are prepared for this session.");
    auto state=std::make_shared<CharacterState>(active->characters());
    for(unsigned i=0;i<4;++i)state->roster.request(i,selected[i]);
    characters.store(std::move(state));
}
void begin_character_menu() {
    auto active=session.load();if(!active || !active->characters())throw mods::Error("No character library is admitted.");
    auto state=std::make_shared<CharacterState>(active->characters());
    state->selector=std::make_unique<mods::CharacterMenuAdapter>(active->characters());
    characters.store(std::move(state));
}
void begin_track_menu(bool allow_races) {
    auto active=session.load();if(!active)throw mods::Error("No catalogue is admitted for the native menu.");
    menu.store(std::make_shared<MenuState>(active->tracks(),allow_races));
}
void request_scene(const std::string& id,unsigned carrier) {
    auto active=session.load();
    if(!active)throw mods::Error("No custom-content session is prepared.");
    active->request(id,carrier);
}
std::string failure(){std::lock_guard lock(error_mutex);return last_error;}
}
extern "C" void dkr_legacy_character_event(std::uint8_t* rdram,recomp_context* ctx,unsigned event,std::uint32_t roster_address) {
    using namespace dkr::runtime;
    auto state=legacy::characters.load();if(!state)return;
    try {
        std::lock_guard lock(state->mutex);
        const auto original=ctx->r4;
        if(event==0 && state->stage.redirect_spawn({rdram,recomp::mem_size},*ctx))return;
        dkr::mods::dispatch_character_event(state->roster,{rdram,recomp::mem_size},*ctx,event,roster_address);
        if(event==0 && ctx->r4!=original)std::fprintf(stderr,"[legacy][character] racer root %u -> %u\n",unsigned(original),unsigned(ctx->r4));
        if(event==1)for(unsigned i=0;i<unsigned(ctx->r4);++i)std::fprintf(stderr,"[legacy][character] P%u committed %s\n",
            i+1,state->roster.active(i).empty()?"stock":state->roster.active(i).c_str());
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" int dkr_legacy_character_menu(std::uint8_t* rdram,recomp_context* ctx,unsigned event,const std::uint32_t* fields) {
    using namespace dkr::runtime;
    auto state=legacy::characters.load();if(!state || !state->selector)return 0;
    try {
        if(!fields || event>8)throw dkr::mods::Error("Invalid character-menu hook ABI.");
        dkr::mods::CharacterMenuFields addresses;std::copy_n(fields,addresses.size(),addresses.begin());
        dkr::mods::CharacterMenuMemory guest({rdram,recomp::mem_size},addresses);
        const auto p=active_payload();if(!p)throw dkr::mods::Error("Character menu has no selected ROM revision.");
        const dkr::mods::CharacterStageCalls stage{p->stage_spawn,p->stage_free,p->stage_music_fraction,p->stage_particles,
            dkr_character_select_animation_fraction,p->stage_camera};
        if(event==0) {
            auto drum=*ctx,tt=*ctx;
            p->unlock_drumstick(rdram,&drum);p->unlock_tt(rdram,&tt);
            {std::lock_guard lock(state->mutex);state->selector->reset();state->selector->set_unlocks(drum.r2!=0,tt.r2!=0);}
            state->stage.initialize({rdram,recomp::mem_size},addresses,*ctx,stage,state->selector->entries());
            // These small banks intentionally remain resident until guest
            // shutdown: native queued sound events may outlive menu exit. Reuse
            // them on subsequent visits, without accumulating allocations.
            if(state->sound_banks.empty()) {
                const auto active=legacy::session.load();
                for(unsigned i=0;i<state->selector->entries().size();++i) {
                    const auto& audio=state->selector->entries()[i].audio;
                    auto allocation=*ctx;allocation.r4=audio.control.size();allocation.r5=0x7f7f7fff;
                    p->asset_allocate(rdram,&allocation);const auto base=static_cast<std::uint32_t>(allocation.r2);
                    if(!base)throw dkr::mods::Error("Not enough guest memory for custom selection audio.");
                    guest.bytes(base,audio.control);
                    auto relocate=*ctx;relocate.r4=static_cast<gpr>(static_cast<std::int32_t>(base));
                    relocate.r5=active->character_sample_address(i);p->sound_bank_relocate(rdram,&relocate);
                    state->sound_banks.push_back(guest.read(base+4));
                }
            }
            state->menu_active=true;
            const auto active=legacy::session.load();
            std::vector<std::uint32_t> samples;
            for(unsigned i=0;i<state->selector->entries().size();++i)samples.push_back(active->character_race_sample_address(i));
            state->presentation.initialize({rdram,recomp::mem_size},*ctx,legacy::presentation_calls(*p),state->selector->entries(),samples);
            std::fprintf(stderr,"[legacy][stage] %zu additional animated selection actors loaded\n",state->selector->entries().size());
        } else if(event==2)return 0; // Original Player Select and OK text only.
        else if(event==6) {
            dkr::mods::CharacterMenuView view;
            {std::lock_guard lock(state->mutex);view=state->selector->view(guest);}
            return state->stage.update({rdram,recomp::mem_size},addresses,*ctx,stage,view);
        } else if(event==7) {
            // Checked call site keeps the physical cursor index in s0.
            const auto player=static_cast<unsigned>(ctx->r16);
            if(player>=4)throw dkr::mods::Error("Native stage cursor is invalid.");
            std::lock_guard lock(state->mutex);
            if(state->selector->custom(player))ctx->r2=static_cast<gpr>(-1);
        } else if(event==3){state->menu_active=false;state->stage.release({rdram,recomp::mem_size},*ctx,stage);}
        else if(event==8) {
            if(!state->menu_active)return 0;
            const auto handle=static_cast<std::uint32_t>(ctx->r5);
            const auto handles=guest.address(dkr::mods::CharacterMenuField::SoundHandles);
            if(handle<handles || handle>=handles+16 || (handle-handles)%4)return 0;
            const unsigned player=(handle-handles)/4;std::optional<std::size_t> selected;
            {std::lock_guard lock(state->mutex);selected=state->selector->custom(player);}
            if(!selected)return 0;
            const auto sound=static_cast<unsigned>(ctx->r4);
            constexpr unsigned bases[]{0x87,0x93,0x19e};unsigned action=3;
            for(unsigned a=0;a<3;++a)if(sound>=bases[a] && sound<bases[a]+10)action=a;
            if(action==3)return 0; // unrelated native audio retains its path
            const auto& cue=state->selector->entries().at(*selected).audio.cues[action];
            if(!cue.sound){guest.write(handle,0);return 1;}
            auto play=*ctx;play.r4=static_cast<gpr>(static_cast<std::int32_t>(state->sound_banks.at(*selected)));
            play.r5=cue.sound;play.r6=cue.priority;play.r7=static_cast<gpr>(static_cast<std::int32_t>(handle));
            p->sound_bank_play(rdram,&play);
            const auto voice=guest.read(handle);
            if(voice) {
                auto param=*ctx;param.r4=static_cast<gpr>(static_cast<std::int32_t>(voice));param.r5=8;param.r6=cue.volume*256;
                p->sound_parameter(rdram,&param);
                param=*ctx;param.r4=static_cast<gpr>(static_cast<std::int32_t>(voice));param.r5=16;
                param.r6=std::bit_cast<std::uint32_t>(cue.pitch/100.0f);p->sound_parameter(rdram,&param);
            }
            return 1;
        }
        else if(event==4) {
            std::lock_guard lock(state->mutex);
            state->selector->commit(guest,state->roster,static_cast<unsigned>(ctx->r4));
        } else {
            if(event==5) {std::lock_guard lock(state->mutex);if(!state->selector->has_custom())return 0;}
            if(!p->filtered_cheats)throw dkr::mods::Error("Character menu cheat policy is unavailable.");
            auto cheats=*ctx;p->filtered_cheats(rdram,&cheats);
            const bool duplicates=(static_cast<std::uint32_t>(cheats.r2)&(1U<<22))!=0;
            std::vector<unsigned> sounds;
            {std::lock_guard lock(state->mutex);
                if(event==1)sounds=state->selector->input(guest,duplicates);
                else {
                    const bool moved=state->selector->native_move(guest,static_cast<unsigned>(ctx->r4),
                        static_cast<std::uint32_t>(ctx->r5),static_cast<unsigned>(ctx->r6),duplicates);
                    const auto stack=static_cast<std::uint32_t>(ctx->r29);
                    if(stack>0xffffffffU-16)throw dkr::mods::Error("Character sound argument overflow.");
                    sounds.push_back(moved?static_cast<unsigned>(ctx->r7):guest.read(stack+16));
                }
            }
            for(auto id:sounds) {
                if(!p->menu_sound_play)throw dkr::mods::Error("Character menu sound callback is unavailable.");
                auto sound=*ctx;sound.r4=id;sound.r5=0;p->menu_sound_play(rdram,&sound);
            }
            if(event==5)return 1;
        }
        return 0;
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" std::uint32_t dkr_legacy_character_portrait_lookup(std::uint8_t* rdram,recomp_context* ctx,std::uint32_t racer_base) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load(); // Hold ownership through guest callbacks.
    if(!state || !state->selector)return 0;
    try {
        const auto p=active_payload();if(!p)throw dkr::mods::Error("Missing portrait revision.");
        auto settings=*ctx;p->get_settings(rdram,&settings);
        const auto base=std::uint32_t(settings.r2);
        // Native sites pass Settings + racer_index * sizeof(Racer). The
        // original character lookup at +0x59 is otherwise left intact.
        if(racer_base<base || (racer_base-base)%0x18 || (racer_base-base)/0x18>=4)return 0;
        std::lock_guard lock(state->mutex);
        return state->presentation.portrait(state->roster,state->selector->entries(),(racer_base-base)/0x18);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" unsigned dkr_legacy_character_race_sound(std::uint8_t* rdram,recomp_context*,std::uint32_t racer,unsigned sound) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return sound;
    try {
        dkr::mods::CharacterMenuFields fields;fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram,recomp::mem_size},fields);
        const auto player=guest.read(racer,2),native_character=guest.read(racer+3,1);
        std::lock_guard lock(state->mutex);
        return state->presentation.sound(state->roster,state->selector->entries(),player,native_character,sound);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" int dkr_legacy_character_play_sound(std::uint8_t* rdram,recomp_context* ctx,unsigned kind) {
    using namespace dkr::runtime;
    const auto state=legacy::characters.load();if(!state || !state->selector)return 0;
    try {
        const auto p=active_payload();if(!p)throw dkr::mods::Error("Missing custom audio revision.");
        return state->presentation.play({rdram,recomp::mem_size},*ctx,legacy::presentation_calls(*p),state->selector->entries(),kind);
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& e){legacy::fail(e.what());}
}
extern "C" int dkr_legacy_track_menu(std::uint8_t* rdram,recomp_context* ctx,unsigned event,const std::uint32_t* fields,unsigned observed) {
    using namespace dkr::runtime;
    auto state=legacy::menu.load();if(!state)return 0;
    try {
        if(!fields)throw dkr::mods::Error("Native menu hook has no verified revision fields.");
#if DKR_LEGACY_QUALIFICATION
        legacy::qualify_native_menu(rdram,ctx,event,fields,false);
#endif
        dkr::mods::TrackMenuFields addresses;std::copy_n(fields,addresses.size(),addresses.begin());
        if(event==1) {
            std::size_t bytes=0;
            {std::lock_guard lock(state->mutex);if(state->adapter.needs_names())bytes=state->adapter.name_bytes().size();}
            if(bytes) {
                const auto payload=active_payload();
                if(!payload || !payload->asset_allocate)throw dkr::mods::Error("Native menu heap callback is unavailable.");
                auto allocation=*ctx;allocation.r4=bytes;allocation.r5=0x7f7f7fff;
                // Original guest allocation may yield: never hold a host mutex.
                payload->asset_allocate(rdram,&allocation);
                std::lock_guard lock(state->mutex);
                state->adapter.install_names({rdram,recomp::mem_size},static_cast<std::uint32_t>(allocation.r2));
            }
        }
        dkr::mods::TrackMenuEffect effect;
        {std::lock_guard lock(state->mutex);effect=state->adapter.apply(event,{rdram,recomp::mem_size},addresses,
            static_cast<std::uint32_t>(ctx->r4),observed);}
        if(effect.scene)legacy::request_scene(effect.scene->id,effect.scene->carrier);
        if(effect.navigation_sound) {
            const auto payload=active_payload();
            if(!payload || !payload->menu_sound_play)throw dkr::mods::Error("Native menu sound callback is unavailable.");
            // Match stock trackmenu_input: SOUND_MENU_PICK2 (0xEB), no handle.
            // Guest audio can yield: call only after releasing the host lock,
            // and preserve the original hook's register context.
            auto sound=*ctx;sound.r4=0xEB;sound.r5=0;
            payload->menu_sound_play(rdram,&sound);
        }
#if DKR_LEGACY_QUALIFICATION
        legacy::qualify_native_menu(rdram,ctx,event,fields,true);
#endif
        if(effect.override_return)ctx->r2=static_cast<gpr>(static_cast<std::int32_t>(effect.return_value));
        return effect.override_return;
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" void dkr_legacy_scene_begin(std::uint8_t* rdram,recomp_context* ctx) {
    using namespace dkr::runtime;
    auto active=legacy::session.load();if(!active)return;
    try {
        const auto before=active->current_content();
        active->begin_scene({rdram,recomp::mem_size},static_cast<std::uint32_t>(ctx->r4));
        const auto after=active->current_content();
        if(before!=after)std::fprintf(stderr,"[legacy][scene] generation=%llu carrier=%u content=%s\n",
            static_cast<unsigned long long>(active->published_scenes()),static_cast<unsigned>(ctx->r4),
            after.empty()?"stock":after.c_str());
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" int dkr_legacy_asset_api(std::uint8_t* rdram,recomp_context* ctx,unsigned operation) {
    using namespace dkr::runtime;
    auto active=legacy::session.load();if(!active)return 0;
    try {
        auto lease=active->acquire();const auto mount=lease.route();if(!mount)return 0;
        const auto payload=active_payload();
        if(!payload)throw dkr::mods::Error("The custom asset loader has no selected revision.");
        return dkr::mods::dispatch_asset_api(static_cast<dkr::mods::AssetOperation>(operation),mount,
            {rdram,recomp::mem_size},*ctx,{payload->asset_allocate,payload->asset_release,payload->asset_copy});
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
extern "C" void dkr_legacy_pi_start_dma(std::uint8_t* rdram,recomp_context* ctx) {
    using namespace dkr::runtime;
    try {
        auto active=legacy::session.load();
        dkr::mods::dispatch_pi_dma(active?&active->bus():nullptr,{rdram,recomp::mem_size},*ctx,
            osPiStartDma_recomp,{nullptr,[](void*,std::uint32_t queue) {
                ultramodern::enqueue_external_message_src(static_cast<std::int32_t>(queue),0,false,
                                                         ultramodern::EventMessageSource::Pi);
            }});
    } catch(const ultramodern::thread_terminated&){throw;}
      catch(const std::exception& error){legacy::fail(error.what());}
}
