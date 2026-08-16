#include "glide_renderer.hpp"

#include "game_registration.hpp"

#include "librecomp/game.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

namespace {

#if defined(DKR_TARGET_WIN95)
// The size of the snapshot the graphics thread receives. It is fixed in
// `submit_rsp_task` (events.cpp) at 8 MiB, the N64's expanded RDRAM -- **not the
// 20 MiB patch 0017 sizes librecomp's space to**, which covers far more than the
// guest's RDRAM.
//
// Getting this wrong would not crash: the decoder bounds every range against
// this size, so too large a value turns an access past the snapshot into a read
// of neighbouring memory, and too small a value rejects legitimate geometry by
// counting it as an invalid address.
constexpr unsigned kSnapshotBytes = 0x800000u;

// This machine's Voodoo 2. 640x480 is the mode E05 measured, and the only one
// this port opens for now.
constexpr int kWidth = 640;
constexpr int kHeight = 480;

// --- Read the game's state, rather than inferring it from the rendering ------
//
// Every measurement so far described the same steady state -- same command count
// per list, same background colour, no depth sorting at all. They say what the
// game draws, never **where it has got to**. Going on perfecting the rendering of
// a frame the game may have no intention of advancing would misspend the effort.
//
// `gGameMode` is the variable DKR itself consults to decide what to do. Its
// address comes from the decomp's symbol map:
//
//     0x801234ec  gGameMode          -1 INTRO, 0 INGAME, 1 MENU, 5 LOCKUP
//     0x800dd394  gLevelLoadTimer
//     0x80121168  gCurrentLevelHeader
//
// `GAMEMODE_LOCKUP` deserves a mention: the game puts itself there when
// `get_lockup_status()` returns true, and shows a crash screen. If it is there,
// no rendering fix will change anything.
//
// **The read is native, with no byte swapping.** librecomp's RDRAM is XOR-3
// interleaved, and for an aligned 32-bit word the interleave and the host's
// little-endianness cancel exactly. Flipping the bytes "to fix the endianness"
// is the mistake already made once this session, on reading `curRDPTask`.
constexpr unsigned kAddrGameMode = 0x1234ECu;
constexpr unsigned kAddrLevelLoadTimer = 0x0DD394u;
constexpr unsigned kAddrLevelHeader = 0x121168u;

int read_word(const std::uint8_t* rdram, unsigned address) {
    int v = 0;
    std::memcpy(&v, rdram + address, sizeof(v));
    return v;
}

// The decoder trace, bounded -- and **two budgets, not one**.
//
// Without a trace one reads "one reject per list" and cannot tell which: the
// counter says there is a problem, the trace says which one. It is bounded
// because the log goes to an emulated floppy, and because the first few
// occurrences suffice -- a reject repeating six hundred times is diagnosed on
// the first.
//
// A single budget already cost a round trip: the deferred commands, far more
// numerous, exhausted it before a single reject had been written. The log then
// showed "589 opcode rejects" without saying which -- the trace meant to say
// which had been spent on noise.
//
// The reject is what we are after; the rest is context. They cannot draw from
// the same bucket.
unsigned g_trace_rejects = 24;
unsigned g_trace_context = 24;

bool starts_with(const char* line, const char* prefix) {
    while (*prefix != '\0') {
        if (*line != *prefix) { return false; }
        line++;
        prefix++;
    }
    return true;
}

void trace_decoder(void*, const char* line) {
    unsigned* bucket = starts_with(line, "REJECT") ? &g_trace_rejects : &g_trace_context;
    if (*bucket == 0) { return; }
    (*bucket)--;
    std::fprintf(stderr, "[gfx][f3d] %s\n", line);
}
#endif

} // namespace

dkr::runtime::GlideRenderer::GlideRenderer() {
    setup_result = ultramodern::renderer::SetupResult::Success;
    chosen_api = ultramodern::renderer::GraphicsApi::Auto;

#if defined(DKR_TARGET_WIN95)
    dkr_render_backend_glide(&backend_);
    if (backend_.open != nullptr && backend_.open(backend_.self, kWidth, kHeight) != 0) {
        opened_ = true;
        width_ = kWidth;
        height_ = kHeight;
        std::fprintf(stderr, "[boot][gfx] Glide ouvert en %dx%d\n", width_, height_);
    } else {
        // **Ne pas faire échouer le démarrage pour autant.**
        //
        // `valid()` rendant faux arrête le fil graphique, et avec lui le jeu.
        // Or une Voodoo absente ou occupée est exactement la situation où l'on
        // veut encore pouvoir lire un journal de démarrage. Le contexte reste
        // donc valide et ne dessine rien, ce qui est le comportement de
        // `DiagnosticRenderer` — une dégradation, pas une panne.
        std::fprintf(stderr, "[boot][gfx] Glide indisponible : rendu desactive\n");
    }
#endif
}

bool dkr::runtime::GlideRenderer::valid() {
    return true;
}

bool dkr::runtime::GlideRenderer::update_config(
    const ultramodern::renderer::GraphicsConfig&,
    const ultramodern::renderer::GraphicsConfig&) {
    return false;
}

void dkr::runtime::GlideRenderer::enable_instant_present() {}

void dkr::runtime::GlideRenderer::send_dl(const OSTask* task,
                                          [[maybe_unused]] std::uint8_t* rdram_snapshot) {
    const auto index = ++display_list_count_;

#if defined(DKR_TARGET_WIN95)
    if (!opened_ || rdram_snapshot == nullptr) {
        return;
    }

    backend_.begin_frame(backend_.self, 0x000000);

    // Reset for every display list, not carried across frames. That is the
    // microcode's contract: each graphics task arrives with its own DMAOffsets
    // and its own matrix. Keeping state would make one frame depend on the
    // previous, and the first symptom would be geometry correct at boot then
    // drifting -- the worst case to diagnose.
    dkr_f3d_init(&context_, rdram_snapshot, kSnapshotBytes, &backend_);
    // librecomp's XOR-3 interleaved layout. Without this flag the decoder would
    // read plausible opcodes at absurd addresses.
    context_.rdram_native = 1;
    context_.trace = trace_decoder;
    // `DKR_NO_DEPTH=1` turns depth sorting off. A diagnostic switch: it answers
    // in one run a question that reading the code does not settle.
    {
        static const bool no_depth = (std::getenv("DKR_NO_DEPTH") != nullptr);
        context_.no_depth = no_depth ? 1 : 0;
    }
    // The resolution actually opened: the decoder needs it to carry the game's
    // buffer (320 wide) onto the screen, for the 2D rectangles as well as for
    // the 3D viewport.
    context_.screen_width = static_cast<unsigned>(width_);
    context_.screen_height = static_cast<unsigned>(height_);

    // **One whole list, dumped once.**
    //
    // The counters led this far then went quiet: 70 commands per list, constant
    // from list 300 on, two fills and nothing else. A stable number says nothing
    // more about what the game is building; the list has to be seen.
    //
    // The 300th is not an arbitrary choice: that is where the rate settles, so
    // it is the first list describing the state the game stays in. Dumping the
    // first would give the initialisation sequence instead.
    if (index == 300) {
        g_trace_context = 400;
        std::fprintf(stderr, "[gfx] --- list 300, full contents ---\n");
    }
    dkr_transform_set_viewport(&context_.transform,
                               static_cast<float>(width_) * 0.5F,
                               -static_cast<float>(height_) * 0.5F,
                               static_cast<float>(width_) * 0.5F,
                               static_cast<float>(height_) * 0.5F);

    // The address is guest-virtual (0x80xxxxxx); the snapshot is indexed
    // physically.
    (void)dkr_f3d_run(&context_, task->t.data_ptr & 0x00FFFFFFu);

    backend_.present(backend_.self);

    for (int i = 0; i < DKR_F3D_REJECT_COUNT_MAX; i++) {
        rejects_by_kind_[i] += context_.state.rejects[i];
    }
    for (int i = 0; i < 256; i++) { opcodes_[i] += context_.state.opcodes[i]; }
    total_rects_ += context_.state.rects;
    total_viewports_ += context_.state.viewports;
    total_tex_loaded_ += context_.state.textures_loaded;
    total_tex_reused_ += context_.state.textures_reused;
    total_tex_refused_ += context_.state.textures_refused;
    total_tex_padded_ += context_.state.textures_padded;
    total_emitted_textured_ += context_.state.emitted_textured;
    // **Accumulate every frame, not inside the reporting block.**
    //
    // The first version of this counter added inside the `if` that only prints
    // one list in sixty: it therefore saw one sixtieth of the frames, and the
    // total was sixty times too low. It was too low *consistently*, which is the
    // worst case -- 5,731 against 380,000 emitted reads as "blending is almost
    // never set" rather than as a sampling error.
    for (int m = 0; m < 8; m++) { blend_[m] += context_.state.emitted_per_blend[m]; }
    total_alpha_test_ += context_.state.emitted_alpha_test;
    for (int i = 0; i < 4; i++) {
        area_[i] += context_.state.area[i];
        depth_[i] += context_.state.emitted_per_depth[i];
    }
    for (int i = 0; i < DKR_COMBINE_COUNT; i++) {
        emitted_per_combine_[i] += context_.state.emitted_per_combine[i];
    }
    total_tex_aspect_ += context_.state.textures_bad_aspect;
    total_tex_unsupported_ += context_.state.textures.unsupported;
    total_tex_out_of_rdram_ += context_.state.textures.out_of_rdram;
    total_states_ += context_.state.states_applied;
    total_combiners_known_ += context_.state.combiners_known;
    total_combiners_unknown_ += context_.state.combiners_unknown;
    total_approximate_ += context_.state.states_approximate;
    total_fill_wrong_cycle_ += context_.state.fills_wrong_cycle;
    total_deferred_ += context_.state.deferred;
    total_commands_ += context_.state.commands;
    total_triangles_ += context_.state.triangles;
    total_emitted_ += context_.state.emitted;
    {
        int i;
        for (i = 0; i < DKR_F3D_REJECT_COUNT_MAX; i++) {
            total_rejects_ += context_.state.rejects[i];
        }
    }

    // A periodic report rather than a line per frame: the log goes to an
    // emulated floppy, and sixty lines a second saturate it.
    //
    // The three numbers are read together, and it is their gap that informs.
    // Many triangles and zero emitted points at clipping or culling; zero
    // triangles with commands points at the decoder; rising rejects point at
    // addressing, hence at the memory layout.
    if (index <= 3 || index % 60 == 0) {
        std::fprintf(stderr,
                     "[gfx] liste=%llu cmd=%lu tri=%lu emis=%lu rejets=%lu\n",
                     static_cast<unsigned long long>(index), total_commands_,
                     total_triangles_, total_emitted_, total_rejects_);
        // The reject total does not say what to fix: an address outside RDRAM
        // accuses the addressing, an unknown opcode the decoding, a vertex index
        // a command missed upstream. Counting them apart is what turns "it
        // rejects" into a lead.
        std::fprintf(stderr,
                     "[gfx]   differees=%lu | adresse=%lu nombre=%lu index=%lu "
                     "profondeur=%lu opcode=%lu\n",
                     total_deferred_,
                     rejects_by_kind_[DKR_F3D_REJECT_ADDRESS],
                     rejects_by_kind_[DKR_F3D_REJECT_COUNT],
                     rejects_by_kind_[DKR_F3D_REJECT_INDEX],
                     rejects_by_kind_[DKR_F3D_REJECT_DEPTH],
                     rejects_by_kind_[DKR_F3D_REJECT_OPCODE]);
        // The eight most frequent opcodes, descending. Eight is enough: the
        // distribution is very uneven, and what we are after is what dominates
        // the frame, not the tail.
        {
            char ligne[160];
            int pris[8] = {0};
            int n = 0;
            std::size_t ecrit = 0;
            ligne[0] = '\0';
            for (n = 0; n < 8; n++) {
                int meilleur = -1;
                for (int op = 0; op < 256; op++) {
                    bool deja = false;
                    for (int k = 0; k < n; k++) { deja = deja || (pris[k] == op); }
                    if (deja || opcodes_[op] == 0) { continue; }
                    if (meilleur < 0 || opcodes_[op] > opcodes_[meilleur]) { meilleur = op; }
                }
                if (meilleur < 0) { break; }
                pris[n] = meilleur;
                ecrit += static_cast<std::size_t>(std::snprintf(
                    ligne + ecrit, sizeof(ligne) - ecrit, " %02X:%lu",
                    static_cast<unsigned>(meilleur), opcodes_[meilleur]));
                if (ecrit >= sizeof(ligne) - 12) { break; }
            }
            std::fprintf(stderr, "[gfx]   opcodes%s\n", ligne);
        }
        // The drawing commands, named and looked for explicitly.
        //
        // The histogram's top eight are all RDP state, which leaves a question
        // the ranking does not settle: is there *any* drawing command in these
        // frames, or none? An absent opcode appears in no ranking, and "absent
        // from the top eight" reads far too easily as "rare" when it may be
        // zero.
        //
        // Zero everywhere would say the boot sequence draws nothing at all.
        // Rectangles without triangles would name the 2D path as the only work
        // left.
        std::fprintf(stderr,
                     "[gfx]   dessin: sommets=%lu triangles=%lu texrect=%lu "
                     "texrectflip=%lu fillrect=%lu | remis=%lu couleur=0x%06X "
                     "tampon=%u\n",
                     opcodes_[0x04], opcodes_[0x05], opcodes_[0xE4],
                     opcodes_[0xE5], opcodes_[0xF6], total_rects_,
                     context_.state.fill_color_argb,
                     context_.state.color_image_width);
        // L'etat RDP. `approches` est le chiffre a surveiller : une traduction
        // approximative qui ne s'annonce pas produit une image plausible et
        // fausse, ce qui est pire qu'un echec franc. `hors-cycle` est un
        // controle qui se declenche tout seul — le RDP ne remplit qu'en mode
        // FILL, donc toute autre valeur accuse le decodage du mot de mode.
        std::fprintf(stderr,
                     "[gfx]   etat: appliques=%lu approches=%lu "
                     "remplissages-hors-cycle=%lu cycle=%u fenetres=%lu\n",
                     total_states_, total_approximate_, total_fill_wrong_cycle_,
                     static_cast<unsigned>(context_.state.current_cycle),
                     total_viewports_);
        // Les textures. `chargees` contre `reutilisees` dit si le cache tient —
        // sans lui on reconvertirait la meme texture des milliers de fois par
        // image, ce qui suffirait a rendre le portage injouable. `inconnues`
        // compte les formats indexes, refuses faute de palette : ils sortent en
        // surfaces sans texture plutot qu'en couleurs arbitraires.
        std::fprintf(stderr,
                     "[gfx]   textures: chargees=%lu reutilisees=%lu "
                     "refusees-tmu=%lu format-inconnu=%lu hors-rdram=%lu\n",
                     total_tex_loaded_, total_tex_reused_,
                     total_tex_refused_, total_tex_unsupported_, total_tex_out_of_rdram_);
        std::fprintf(stderr,
                     "[gfx]   refus-detail: proportions=%lu taille=%lu "
                     "emplacements=%lu memoire-tmu=%lu\n",
                     dkr_glide_backend_upload_failure(0),
                     dkr_glide_backend_upload_failure(1),
                     dkr_glide_backend_upload_failure(2),
                     dkr_glide_backend_upload_failure(3));
        std::fprintf(stderr,
                     "[gfx]   remplies-en-puissance-de-2=%lu "
                     "refusees-proportions=%lu\n",
                     total_tex_padded_, total_tex_aspect_);
        // Les coordonnées normalisées. Un voisinage de [0,1] confirme le format
        // 10.5 et la largeur employée ; des milliers le réfutent.
        std::fprintf(stderr,
                     "[gfx]   emis: avec-texture=%lu | shade=%lu texel=%lu "
                     "texel*shade=%lu texel*shade+a=%lu\n",
                     total_emitted_textured_, emitted_per_combine_[0],
                     emitted_per_combine_[1], emitted_per_combine_[2],
                     emitted_per_combine_[3]);
        std::fprintf(stderr,
                     "[gfx]   aires: <1px=%lu <100px=%lu <10000px=%lu "
                     ">=10000px=%lu\n",
                     area_[0], area_[1], area_[2], area_[3]);
        std::fprintf(stderr,
                     "[gfx]   profondeur: mode0=%lu mode1=%lu mode2=%lu mode3=%lu\n",
                     depth_[0], depth_[1], depth_[2],
                     depth_[3]);
        {
            char l2[128];
            std::size_t e2 = 0;
            int m;
            l2[0] = '\0';
            for (m = 0; m < 8; m++) {
                if (blend_[m] != 0) {
                    e2 += static_cast<std::size_t>(std::snprintf(
                        l2 + e2, sizeof(l2) - e2, " %d:%lu", m, blend_[m]));
                }
            }
            std::fprintf(stderr,
                         "[gfx]   melange:%s | test-alpha=%lu ref-max=%u\n",
                         l2, total_alpha_test_, context_.state.alpha_ref_max);
        }
        if (context_.state.oow_max > context_.state.oow_min) {
            total_tex_black_ += context_.state.textures_black;
            total_tex_with_content_ += context_.state.textures_with_content;
            std::fprintf(stderr, "[gfx]   texels: noires=%lu avec-contenu=%lu\n",
                         total_tex_black_, total_tex_with_content_);
            std::fprintf(stderr, "[gfx]   shade-max=%d alpha-max=%d\n",
                         static_cast<int>(context_.state.shade_max),
                         static_cast<int>(context_.state.alpha_max));
            std::fprintf(stderr, "[gfx]   oow=[%d..%d]/1000000\n",
                         static_cast<int>(context_.state.oow_min * 1000000.0F),
                         static_cast<int>(context_.state.oow_max * 1000000.0F));
        }
        {
            char ligne[128];
            std::size_t ecrit = 0;
            unsigned i;
            ligne[0] = '\0';
            for (i = 0; i < context_.state.unknown_keys_n && ecrit < 100; i++) {
                ecrit += static_cast<std::size_t>(std::snprintf(
                    ligne + ecrit, sizeof(ligne) - ecrit, " %08X",
                    static_cast<unsigned>(context_.state.unknown_keys[i])));
            }
            std::fprintf(stderr,
                         "[gfx]   combineurs: repertories=%lu inconnus=%lu%s%s\n",
                         total_combiners_known_, total_combiners_unknown_,
                         (ligne[0] != '\0') ? " cles:" : "", ligne);
            // La composition, sous la forme (a,b,c,d) que `gDPSetCombineLERP`
            // prend — c'est celle des macros G_CC_*, donc celle qui permet de
            // nommer la configuration et de l'ajouter à la table.
            for (i = 0; i < context_.state.unknown_keys_n; i++) {
                const dkr_combiner& k = context_.state.unknown_combiners[i];
                std::fprintf(stderr,
                             "[gfx]     %08X cycle=%u rgb0=(%u,%u,%u,%u) "
                             "a0=(%u,%u,%u,%u) rgb1=(%u,%u,%u,%u) "
                             "a1=(%u,%u,%u,%u)\n",
                             static_cast<unsigned>(context_.state.unknown_keys[i]),
                             context_.state.unknown_cycle[i],
                             k.rgb[0].a, k.rgb[0].b, k.rgb[0].c, k.rgb[0].d,
                             k.alpha[0].a, k.alpha[0].b, k.alpha[0].c, k.alpha[0].d,
                             k.rgb[1].a, k.rgb[1].b, k.rgb[1].c, k.rgb[1].d,
                             k.alpha[1].a, k.alpha[1].b, k.alpha[1].c, k.alpha[1].d);
            }
        }
        // L'état du jeu, lu chez lui. C'est la seule mesure de cette série qui
        // ne parle pas du rendu.
        {
            const int mode = read_word(rdram_snapshot, kAddrGameMode);
            static const char* noms[] = { "INGAME", "MENU", "UNUSED2",
                                          "UNUSED3", "UNUSED4", "LOCKUP" };
            const char* nom = (mode == -1) ? "INTRO"
                            : (mode >= 0 && mode <= 5) ? noms[mode] : "?";
            std::fprintf(stderr,
                         "[jeu] gGameMode=%d (%s) chargement=%d niveau=0x%08X\n",
                         mode, nom, read_word(rdram_snapshot, kAddrLevelLoadTimer),
                         static_cast<unsigned>(
                             read_word(rdram_snapshot, kAddrLevelHeader)));
        }
        if (context_.state.s_max > context_.state.s_min) {
            std::fprintf(stderr,
                         "[gfx]   coords: s=[%d..%d]/1000 t=[%d..%d]/1000\n",
                         static_cast<int>(context_.state.s_min * 1000.0F),
                         static_cast<int>(context_.state.s_max * 1000.0F),
                         static_cast<int>(context_.state.t_min * 1000.0F),
                         static_cast<int>(context_.state.t_max * 1000.0F));
        }
    }
#else
    (void)task;
    (void)index;
#endif
}

void dkr::runtime::GlideRenderer::update_screen() {
    const auto index = ++present_count_;
    if (index == 1) {
        // Le premier rafraîchissement n'est mis en file qu'une fois que le fil
        // VI d'ultramodern a installé son mode factice. Démarrer DKR ici évite
        // que le fil de jeu ne coure après cette mise en place.
        std::fprintf(stderr, "[boot] VI initialized; starting recompiled DKR entrypoint\n");
        recomp::start_game(kGameId);
    }
    if (index <= 10 || index % 300 == 0) {
        std::fprintf(stderr, "[boot][vi] present=%llu\n",
                     static_cast<unsigned long long>(index));
    }
}

void dkr::runtime::GlideRenderer::shutdown() {
#if defined(DKR_TARGET_WIN95)
    if (opened_ && backend_.close != nullptr) {
        backend_.close(backend_.self);
        opened_ = false;
        // La carte garde l'écran par relais analogique : ne pas refermer le
        // contexte laisse le moniteur sur la sortie 3dfx, écran noir, sans
        // qu'aucun message d'erreur ne soit visible.
        std::fprintf(stderr, "[gfx] Glide referme\n");
    }
#endif
}

std::uint32_t dkr::runtime::GlideRenderer::get_display_framerate() const {
    return 60;
}

float dkr::runtime::GlideRenderer::get_resolution_scale() const {
    return 1.0F;
}

std::unique_ptr<ultramodern::renderer::RendererContext> dkr::runtime::CreateGlideRenderer(
    std::uint8_t*, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<GlideRenderer>();
}
