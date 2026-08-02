#pragma once

#include "ultramodern/ultra64.h"

#include <cstdint>

namespace RT64 {
struct Application;
struct DisplayList;
struct GBI;
struct State;
}

namespace dkr::runtime {

std::uint64_t completed_f3ddkr_task_count();

class F3DDKRRT64Bridge {
public:
    F3DDKRRT64Bridge();
    ~F3DDKRRT64Bridge();

    F3DDKRRT64Bridge(const F3DDKRRT64Bridge&) = delete;
    F3DDKRRT64Bridge& operator=(const F3DDKRRT64Bridge&) = delete;

    void process(RT64::Application& application, const OSTask& task);

private:
    struct StateData;
    RT64::GBI* gbi_;
    StateData* data_;

    static F3DDKRRT64Bridge* active_;

    static void PresentationGroup(RT64::State* state,
                                  RT64::DisplayList** display_list);
    static void Matrix(RT64::State* state, RT64::DisplayList** display_list);
    static void FillRect(RT64::State* state, RT64::DisplayList** display_list);
    static void TextureOffset(RT64::State* state, RT64::DisplayList** display_list);
    static void Vertex(RT64::State* state, RT64::DisplayList** display_list);
    static void Triangle(RT64::State* state, RT64::DisplayList** display_list);
    static void DisplayListBranch(RT64::State* state,
                                  RT64::DisplayList** display_list);
    static void EndDisplayList(RT64::State* state,
                               RT64::DisplayList** display_list);
    static void CountedDisplayList(RT64::State* state, RT64::DisplayList** display_list);
    static void DMAOffsets(RT64::State* state, RT64::DisplayList** display_list);
    static void MoveWord(RT64::State* state, RT64::DisplayList** display_list);
    static void SetTextureImage(RT64::State* state, RT64::DisplayList** display_list);
    static void LoadBlock(RT64::State* state, RT64::DisplayList** display_list);

    static void RunCommands(RT64::State* state, RT64::DisplayList* commands,
                            std::uint32_t command_count);
};

} // namespace dkr::runtime
