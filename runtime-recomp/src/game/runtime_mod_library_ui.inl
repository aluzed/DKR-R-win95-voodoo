// Included by runtime_ui.cpp inside its UI namespace. Presentation only; all
// filesystem/catalogue mutations are dispatched through ModLibrary workers.
bool ModWrappedButton(const char* label,float width,float minimum_height=42) {
    const float wrap=std::max(width-ImGui::GetStyle().FramePadding.x*2-4,1.0F);
    const auto text=ImGui::CalcTextSize(label,nullptr,false,wrap);
    ImGui::PushID(label);
    const bool pressed=ImGui::Button("##wrapped-action",{width,std::max(minimum_height,text.y+ImGui::GetStyle().FramePadding.y*2)});
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
        {min.x+std::max((max.x-min.x-text.x)*0.5F,ImGui::GetStyle().FramePadding.x),min.y+(max.y-min.y-text.y)*0.5F},
        ImGui::GetColorU32(ImGuiCol_Text),label,nullptr,wrap);
    ImGui::PopID();return pressed;
}
bool BeginModLibraryModal(const char* name,ImGuiWindowFlags flags) {
    // Unlike short confirmation modals, imported support notes may be long.
    // This locally scoped modal allows vertical scrolling at the viewport cap.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{26,24});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,18);
    const bool visible=ImGui::BeginPopupModal(name,nullptr,flags);
    ImGui::PopStyleVar(2);return visible;
}
std::string ModCardNotice(const dkr::mods::browser::Card& card) {
    if(card.item.hidden)return "HIDDEN";
    if(!g_mod_browser_revision)return "SELECT A GAME PAK";
    if(!dkr::mods::browser::compatible(card,g_mod_browser_revision))return "NEEDS PREPARATION";
    const auto details=dkr::mods::browser::lower(card.item.details);
    if(details.find("car-only")!=std::string::npos || details.find("car only")!=std::string::npos)return "CAR ONLY";
    if(!card.item.details.empty())return "SUPPORT NOTES IN MANAGE";
    return {};
}

void DrawModManagement(ModBrowserState& browser,bool characters,bool locked) {
    using namespace dkr::mods;
    const auto kind=characters?TrackCatalog::Kind::Character:TrackCatalog::Kind::Track;
    const auto find_card=[&](const std::string& id)->const browser::Card* {
        const auto found=std::find_if(browser.all.begin(),browser.all.end(),[&](const auto& card){return card.item.id==id;});
        return found==browser.all.end()?nullptr:&*found;
    };
    const auto modal_size=[] {
        const auto display=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSizeConstraints({std::max(240.0F,std::min(540.0F,display.x-48)),0},
            {std::max(240.0F,std::min(700.0F,display.x-32)),std::max(200.0F,display.y-48)});
    };
    bool request_hide=false,request_remove=false;
    modal_size();
    if(BeginModLibraryModal("Manage custom mod",ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto* card=find_card(browser.manage_id);
        if(!card)ImGui::TextWrapped("This mod is no longer in the installed library.");
        else {
            const auto& item=card->item;
            ImGui::TextWrapped("%s",characters?"MANAGE CUSTOM CHARACTER":"MANAGE CUSTOM TRACK");
            ImGui::Separator();ImGui::TextWrapped("%s",item.name.c_str());
            DrawColoredWrapped(item.enabled?kAccent:kWarm,item.hidden?"HIDDEN":item.enabled?"ACTIVE":"INACTIVE");
            for(const auto& source:card->sources)ImGui::TextWrapped("SOURCE PACK: %s",source.c_str());
            ImGui::TextWrapped("PREPARED FOR: %s",card->revisions==3?"Game Pak v 1.0 / v 1.1":card->revisions==1?"Game Pak v 1.0":"Game Pak v 1.1");
            ImGui::TextWrapped("MANAGED SIZE: %s (prepared variants; retained import material excluded)",FormatManagedTexturePackSize(item.managed_bytes).c_str());
            if(item.imported_at) {
                const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds{std::chrono::seconds{item.imported_at}})};
                ImGui::Text("IMPORTED: %04d-%02u-%02u",static_cast<int>(date.year()),static_cast<unsigned>(date.month()),static_cast<unsigned>(date.day()));
            } else ImGui::TextWrapped("IMPORTED: Unknown (older import)");
            if(!item.details.empty())DrawColoredWrapped(kWarm,item.details.c_str());
            if(!characters)ImGui::TextWrapped("VEHICLES: %s%s%s",item.vehicles&1?"Car ":"",item.vehicles&2?"Hovercraft ":"",item.vehicles&4?"Plane":"");
            const float width=std::max(ImGui::GetContentRegionAvail().x,1.0F);
            ImGui::Dummy({0,8});
            if(locked)DrawColoredWrapped(kWarm,"Return to the launcher and leave the lobby to change mods. Busy operations must finish first.");
            if(characters&&!item.enabled&&browser::active_count(browser.all)>=MaxActiveStageCharacters)
                ImGui::TextWrapped("Two custom characters are already active. Deactivate one before enabling another.");
            const bool can_enable=browser::can_activate(*card,characters,browser::active_count(browser.all),g_mod_browser_revision,locked);
            ImGui::BeginDisabled(locked || (!item.enabled&&!can_enable));
            if(ModWrappedButton(item.enabled?"DEACTIVATE MOD":"ACTIVATE MOD",width)) {
                g_legacy_imports.set_enabled(kind,item.id,!item.enabled);ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(locked);
            if(ModWrappedButton(item.hidden?"RESTORE TO LIBRARY":"HIDE FROM LIBRARY",width)) {
                if(item.hidden)g_legacy_imports.set_hidden(kind,item.id,false);
                else {browser.hide_id=item.id;request_hide=true;}
                ImGui::CloseCurrentPopup();
            }
            if(ModWrappedButton("REMOVE MOD...",width)) {browser.remove_id=item.id;request_remove=true;ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();
            if(ModWrappedButton("OPEN MANAGED LOCATION",width)) {
                const auto path=g_config_directory/"mods"/"legacy"/(characters?"prepared-characters":"prepared")/item.group/item.storage;
                dkr::runtime::support::open_directory(path,g_legacy_import_status);
            }
            if(ImGui::CollapsingHeader("IMPORT DETAILS")) {
                ImGui::TextWrapped("CONTENT ID: %s",item.id.c_str());
                ImGui::TextWrapped("Source imports are retained for re-preparation. This mod uses isolated saves; hiding or removing it never deletes saves.");
                ImGui::BeginDisabled(locked||item.review.empty());
                if(ModWrappedButton("PREPARE SOURCE IMPORT AGAIN",width)) {
                    std::vector<std::filesystem::path> roms;for(const auto& entry:LoadRomCatalog()){if(roms.size()==8)break;roms.push_back(entry.path);}
                    g_legacy_imports.prepare_review(item.review,std::move(roms));ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("This explicitly restores removed entries from this source import. Existing hidden entries remain hidden.");
            }
        }
        if(ModWrappedButton("CLOSE",ImGui::GetContentRegionAvail().x))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(request_hide)ImGui::OpenPopup("Hide custom mod?");
    if(request_remove)ImGui::OpenPopup("Remove custom mod?");
    for(const bool remove:{false,true}) {
        modal_size();
        if(!BeginModLibraryModal(remove?"Remove custom mod?":"Hide custom mod?",ImGuiWindowFlags_AlwaysAutoResize))continue;
        const auto* card=find_card(remove?browser.remove_id:browser.hide_id);
        if(card) {
            ImGui::TextWrapped("%s",card->item.name.c_str());ImGui::Separator();
            ImGui::TextWrapped(remove?
                "Remove this mod and all its prepared Game Pak variants? Other tracks and characters from the same pack, source files, retained import material and all saves are kept.":
                "Deactivate and hide this mod? Its files and saves are kept. Use Visibility: Hidden to restore the card. Restoring does not activate it.");
            if(remove)DrawColoredWrapped(kWarm,"Prepared content is deleted. Reimport its source patch to install it again.");
            ImGui::BeginDisabled(locked);
            if(ModWrappedButton(remove?"REMOVE THIS MOD":"DEACTIVATE AND HIDE",ImGui::GetContentRegionAvail().x,44)) {
                if(remove)g_legacy_imports.remove(kind,card->item.id);else g_legacy_imports.set_hidden(kind,card->item.id,true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        } else ImGui::TextWrapped("This mod is no longer installed.");
        if(ModWrappedButton("CANCEL",ImGui::GetContentRegionAvail().x,44))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawModCardBrowser(float requested_width,bool characters,const dkr::mods::ModLibraryView& mods,bool mods_locked) {
    using namespace dkr::mods;
    auto& state=g_mod_browsers[characters?1:0];const auto snapshot=characters?mods.characters:mods.tracks;
    const auto kind=characters?TrackCatalog::Kind::Character:TrackCatalog::Kind::Track;
    const bool locked=mods_locked||mods.busy;
    ImGui::PushID(characters?"character-card-library":"track-card-library");
    if(state.snapshot!=snapshot) {state.snapshot=snapshot;state.all=browser::cards(*snapshot,characters);state.filter_key.clear();state.measured_width=-1;}
    const auto active=browser::active_count(state.all);
    if(characters)ImGui::Text("ACTIVE CHARACTERS: %u / %u",active,MaxActiveStageCharacters);
    if(characters&&active>=MaxActiveStageCharacters)DrawColoredWrapped(kWarm,"Deactivate one character to enable another.");
    const float width=std::max(std::min(requested_width,ImGui::GetContentRegionAvail().x),1.0F);
    ImGui::TextDisabled("SEARCH");
    {
        const ControlFontScope scope;
        const char* label=state.search[0]?state.search:characters?"SEARCH CUSTOM CHARACTERS...":"SEARCH CUSTOM TRACKS...";
        if(ModWrappedButton(label,width,ImGui::GetFrameHeight()))RequestTextEntryKeyboard(characters?TextEntryTarget::CustomCharacterSearch:TextEntryTarget::CustomTrackSearch);
    }
    const char* labels[]{"SORT","STATE","COMPATIBILITY","SOURCE PACK","VISIBILITY"};
    const char* choices[]{"Name A-Z\0Name Z-A\0Largest first\0Smallest first\0Newest first\0Oldest first\0",
        "All\0Active\0Inactive\0","All\0Compatible\0Needs attention\0",nullptr,"Visible\0All\0Hidden\0"};
    int* values[]{&state.sort,&state.state,&state.compatibility,nullptr,&state.visibility};
    const int filter_columns=width>=1100?5:width>=700?3:width>=460?2:1;
    for(int first=0;first<5;first+=filter_columns) {
        const int count=std::min(filter_columns,5-first);ImGui::PushID(first);
        if(ImGui::BeginTable("filters",count,ImGuiTableFlags_SizingStretchSame,{width,0})) {
            ImGui::TableNextRow();for(int i=0;i<count;++i){ImGui::TableSetColumnIndex(i);DrawDisabledWrapped(labels[first+i]);}
            ImGui::TableNextRow();for(int i=0;i<count;++i) {
                ImGui::TableSetColumnIndex(i);const int index=first+i;ImGui::PushID(index);
                ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x,1.0F));
                if(index==3) {
                    std::set<std::string> sources;for(const auto& card:state.all)sources.insert(card.sources.begin(),card.sources.end());
                    if(!state.source.empty()&&!sources.contains(state.source))state.source.clear();
                    std::vector<std::string> names{"All"};names.insert(names.end(),sources.begin(),sources.end());
                    std::string entries;int selected=0;for(int n=0;n<static_cast<int>(names.size());++n){entries+=names[n];entries+='\0';if(n&&names[n]==state.source)selected=n;}entries+='\0';
                    if(ControlCombo("##source",&selected,entries.c_str()))state.source=selected?names[selected]:"";
                } else ControlCombo("##filter",values[index],choices[index]);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    const std::string key=std::string(state.search)+"|"+state.source+"|"+std::to_string(state.sort)+":"+std::to_string(state.state)+":"+std::to_string(state.compatibility)+":"+std::to_string(state.visibility)+":"+std::to_string(g_mod_browser_revision);
    if(state.filter_key!=key) {state.shown=browser::select(state.all,{state.search,state.source,state.sort,state.state,state.compatibility,state.visibility,g_mod_browser_revision});state.filter_key=key;state.measured_width=-1;}
    ImGui::TextDisabled("%zu %s SHOWN",state.shown.size(),characters?"CHARACTERS":"TRACKS");
    if(ImGui::Button("RESET FILTERS")) {state.search[0]=0;state.source.clear();state.sort=state.state=state.compatibility=state.visibility=0;}
    ImGui::SameLine();ImGui::BeginDisabled(locked);
    if(ImGui::Button("REFRESH"))g_legacy_imports.refresh();ImGui::EndDisabled();
    bool manage=false;
    if(state.shown.empty())ImGui::TextWrapped(state.all.empty()?"No prepared content yet. Use Import Mods above.":"No mods match these filters. Reset filters or choose Visibility: All.");
    else {
        const auto& style=ImGui::GetStyle();constexpr ImVec2 padding{12,10};constexpr float manage_height=36,bottom_gap=10;
        const int columns=dkr::runtime::texture_browser::responsive_column_count(width,style.ItemSpacing.x,260);
        const float cell=std::max((width-style.ItemSpacing.x*(columns-1))/columns-style.CellPadding.x*2,1.0F);
        const float name_width=std::max(cell-padding.x*2-ImGui::GetFrameHeight()-style.ItemSpacing.x-10,48.0F);
        const float body_width=std::max(cell-padding.x*2,1.0F);
        if(state.measured_width!=name_width||state.measured_font!=ImGui::GetFont()||state.measured_font_size!=ImGui::GetFontSize()||state.measured_generation!=g_font_generation) {
            state.name_height=ImGui::GetTextLineHeight();float note_height=0;
            for(const auto& card:state.shown) {
                state.name_height=std::max(state.name_height,ImGui::CalcTextSize(card.item.name.c_str(),nullptr,false,name_width).y);
                const auto note=ModCardNotice(card);if(!note.empty())note_height=std::max(note_height,ImGui::CalcTextSize(note.c_str(),nullptr,false,body_width).y);
            }
            state.name_height+=4;
            state.card_height=std::max(180.0F,padding.y*2+std::max(state.name_height,ImGui::GetFrameHeight())+style.ItemSpacing.y*3+
                ImGui::CalcTextSize(characters?"CUSTOM CHARACTER":"CUSTOM TRACK",nullptr,false,body_width).y+note_height+manage_height+bottom_gap);
            state.measured_width=name_width;state.measured_font=ImGui::GetFont();state.measured_font_size=ImGui::GetFontSize();state.measured_generation=g_font_generation;
        }
        if(ImGui::BeginTable("cards",columns,ImGuiTableFlags_SizingStretchSame,{width,0})) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>((state.shown.size()+columns-1)/columns),state.card_height+style.CellPadding.y*2);
            while(clipper.Step())for(int index=clipper.DisplayStart*columns;index<std::min(clipper.DisplayEnd*columns,static_cast<int>(state.shown.size()));++index) {
                const auto& card=state.shown[index];
                ImGui::TableNextColumn();ImGui::PushID(card.item.id.c_str());
                if(BeginPaddedChild("mod-card",{0,state.card_height},true,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse,padding)) {
                    bool enabled=card.item.enabled;
                    const bool can_enable=browser::can_activate(card,characters,active,g_mod_browser_revision,locked);
                    ImGui::BeginDisabled(locked||(!enabled&&!can_enable));
                    if(ImGui::Checkbox("##enabled",&enabled))g_legacy_imports.set_enabled(kind,card.item.id,enabled);
                    ImGui::EndDisabled();ImGui::SameLine();
                    if(ImGui::BeginChild("name",{0,state.name_height},false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse))ImGui::TextWrapped("%s",card.item.name.c_str());ImGui::EndChild();
                    DrawColoredWrapped(kAccent,characters?"CUSTOM CHARACTER":"CUSTOM TRACK");
                    const auto note=ModCardNotice(card);if(!note.empty())DrawColoredWrapped(kWarm,note.c_str());
                    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),state.card_height-padding.y-bottom_gap-manage_height));
                    if(ImGui::Button("MANAGE...",{ImGui::GetContentRegionAvail().x,manage_height})){state.manage_id=card.item.id;manage=true;}
                }
                ImGui::EndChild();ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    if(manage)ImGui::OpenPopup("Manage custom mod");
    DrawModManagement(state,characters,locked);
    ImGui::PopID();
}
