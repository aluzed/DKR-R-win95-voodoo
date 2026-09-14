// Headless layout checks of the actual mod-browser drawing implementation.
// No SDL window, renderer, user configuration, ROM or filesystem mutation.
#include "mods/legacy_mod_library.hpp"
#include "mods/legacy_mod_browser.hpp"
#include "texture_pack_browser_policy.hpp"
#include "generated/racing_banana_font.h"
#include "generated/jumpman_font.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <iostream>
#include <cfloat>

namespace dkr::runtime::support {bool open_directory(const std::filesystem::path&,std::string&){return false;}}
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct ModBrowserState {
    char search[160]{};int sort=0,state=0,compatibility=0,visibility=0;
    std::string source,manage_id,remove_id,hide_id;
    std::shared_ptr<const dkr::mods::TrackCatalogView> snapshot;
    std::vector<dkr::mods::browser::Card> all,shown;std::string filter_key;
    float measured_width=-1,measured_font_size=0,name_height=0,card_height=0;
    ImFont* measured_font=nullptr;std::uint64_t measured_generation=0;
};
std::array<ModBrowserState,2> g_mod_browsers;
unsigned g_mod_browser_revision=1;std::uint64_t g_font_generation=1;
std::filesystem::path g_config_directory;std::string g_legacy_import_status;
dkr::mods::ModLibrary g_legacy_imports;
const ImVec4 kAccent{0,1,1,1},kWarm{1,0.7F,0,1};
ImFont* controls=nullptr;
std::vector<ImVec2> filter_positions;
struct ControlFontScope {explicit ControlFontScope(bool=false){ImGui::PushFont(controls);}~ControlFontScope(){ImGui::PopFont();}};
bool ControlCombo(const char* label,int* value,const char* items){ControlFontScope scope;const auto changed=ImGui::Combo(label,value,items);filter_positions.push_back(ImGui::GetItemRectMin());return changed;}
bool BeginPaddedChild(const char* id,const ImVec2& size,bool border,ImGuiWindowFlags flags,const ImVec2& padding){ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,padding);const auto result=ImGui::BeginChild(id,size,border,flags);ImGui::PopStyleVar();return result;}
void DrawColoredWrapped(const ImVec4&,std::string_view text){ImGui::TextWrapped("%.*s",static_cast<int>(text.size()),text.data());}
void DrawDisabledWrapped(std::string_view text){ImGui::TextWrapped("%.*s",static_cast<int>(text.size()),text.data());}
std::string FormatManagedTexturePackSize(std::uintmax_t bytes){return std::to_string(bytes)+" B";}
enum class TextEntryTarget{CustomTrackSearch,CustomCharacterSearch};
void RequestTextEntryKeyboard(TextEntryTarget){}
struct RomEntry{std::filesystem::path path;};
std::vector<RomEntry> LoadRomCatalog(){return {};}
#include "../src/game/runtime_mod_library_ui.inl"

void fonts(float size) {
    auto& io=ImGui::GetIO();
    static const ImWchar text_ranges[]{0x20,0x2f,0x3a,0x7e,0};
    static const ImWchar digits[]{0x30,0x39,0};
    ImFontConfig base{};base.FontDataOwnedByAtlas=false;base.GlyphRanges=text_ranges;
    auto* body=io.Fonts->AddFontFromMemoryTTF(const_cast<char*>(dkr_racing_banana_font),static_cast<int>(dkr_racing_banana_font_size),size,&base,text_ranges);
    ImFontConfig merge{};merge.FontDataOwnedByAtlas=false;merge.MergeMode=true;merge.GlyphRanges=digits;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(dkr_jumpman_font),static_cast<int>(dkr_jumpman_font_size),size*1.42F,&merge,digits);
    ImFontConfig control{};control.SizePixels=size*21/19;controls=io.Fonts->AddFontDefault(&control);io.FontDefault=body;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);check(pixels!=nullptr,"Font atlas failed.");
}
}
int main() {
    try {
        unsigned rendered=0;
        for(const float size:{19.0F,24.0F,30.0F})for(const float width:{400.0F,750.0F,1150.0F,1700.0F})for(const bool characters:{false,true}) {
            ImGui::CreateContext();g_mod_browsers={};fonts(size);
            auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={width+80,800};io.DeltaTime=1.0F/60;
            io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
            // Match the launcher's production spacing, including its larger controls.
            ImGui::GetStyle().WindowPadding={0,0};ImGui::GetStyle().FramePadding={16,11};
            ImGui::GetStyle().CellPadding={4,2};ImGui::GetStyle().ItemSpacing={12,13};
            auto view=std::make_shared<dkr::mods::TrackCatalogView>();
            for(int n=0;n<200;++n) {
                dkr::mods::TrackCatalogItem item;item.id=std::to_string(1000+n);item.name=n%3==0?
                    "A Very Long Custom Name With All Its Words Preserved - Rainbow Road Special Anniversary Edition 2026":
                    n%3==1?"AnExtremelyLongUnbrokenCustomCharacterOrTrackNameThatMustNeverClipOrBeTruncated123456789":"Zebra Track";
                item.revision="us.v77";item.source_name="A Source Pack";item.enabled=n<2;
                if(n%3==1)item.details="Car-only; other vehicle assets are not supplied.";
                view->tracks.push_back(item);
            }
            dkr::mods::ModLibraryView mods;mods.tracks=mods.characters=view;
            for(int frame=0;frame<4;++frame) {
                ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,780});
                ImGui::Begin("Browser",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
                filter_positions.clear();const auto available=ImGui::GetContentRegionAvail().x;
                DrawModCardBrowser(width,characters,mods,false);ImGui::End();ImGui::Render();
                if(frame<2)continue;
                check(filter_positions.size()==5,"Missing filter controls.");
                const int filter_columns=available>=1100?5:available>=700?3:available>=460?2:1;
                for(int first=0;first<5;first+=filter_columns)for(int i=first+1;i<std::min(first+filter_columns,5);++i)
                    check(std::abs(filter_positions[first].y-filter_positions[i].y)<0.5F,"Dropdown row is not aligned.");
                unsigned cards=0;
                for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active) {
                    const std::string name=window->Name;
                    const auto leaf=name.substr(name.find_last_of('/')+1);
                    if(leaf.starts_with("mod-card_")) {
                        ++cards;++rendered;
                        check(std::abs(window->Size.y-g_mod_browsers[characters?1:0].card_height)<1.1F,"Cards have unequal heights.");
                        check(window->ContentSize.y<=window->Size.y-window->WindowPadding.y*2+1,"Card content clips vertically.");
                        check(window->ContentSize.x<=window->Size.x-window->WindowPadding.x*2+1,"Card content clips horizontally.");
                    }
                    if(leaf.starts_with("name_"))
                        check(window->ContentSize.y<=window->Size.y-window->WindowPadding.y*2+1,"Full mod name clips.");
                }
                check(cards<200,"Offscreen cards were all rendered.");
            }
            const auto& state=g_mod_browsers[characters?1:0];check(state.all.size()==200,"Cards lost during layout.");
            // Show a long-notes modal through the production modal renderer.
            auto& panel=g_mod_browsers[characters?1:0];panel.all.front().item.details=std::string(3500,'W');panel.manage_id=panel.all.front().item.id;
            for(int frame=0;frame<3;++frame) {
                ImGui::NewFrame();ImGui::Begin("Modal owner");if(!frame)ImGui::OpenPopup("Manage custom mod");
                DrawModManagement(panel,characters,true);ImGui::End();ImGui::Render();
            }
            bool modal=false;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&(window->Flags&ImGuiWindowFlags_Modal)) {
                modal=true;check(window->Size.y<=io.DisplaySize.y-47,"Manage modal exceeds viewport.");
                check(!(window->Flags&ImGuiWindowFlags_NoScrollbar),"Long mod notes cannot scroll.");
                if(window->ContentSize.x>window->Size.x-window->WindowPadding.x*2-window->ScrollbarSizes.x+1)
                    std::cerr<<"Modal layout font="<<size<<" viewport="<<width<<" content="<<window->ContentSize.x<<" size="<<window->Size.x<<" pad="<<window->WindowPadding.x<<" scrollbar="<<window->ScrollbarSizes.x<<'\n';
                check(window->ContentSize.x<=window->Size.x-window->WindowPadding.x*2-window->ScrollbarSizes.x+1,"Modal text clips horizontally.");
            }
            check(modal,"Manage modal did not open.");ImGui::DestroyContext();
        }
        check(rendered>0,"No card windows were checked.");std::cout<<checks<<" headless real-font card/modal layout checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
