#include "glide_renderer.hpp"

#include "game_registration.hpp"

#include "librecomp/game.hpp"

#include <cstdio>
#include <memory>

namespace {

#if defined(DKR_TARGET_WIN95)
// La taille de l'instantané que le fil graphique reçoit. Elle est fixée dans
// `submit_rsp_task` (events.cpp) et vaut 8 Mio, la RDRAM étendue de la N64 —
// **et non les 20 Mio auxquels le correctif 0017 dimensionne l'espace de
// librecomp**, qui couvre bien plus que la RDRAM invitée.
//
// Se tromper ici ne planterait pas : le décodeur borne chaque plage sur cette
// taille, donc une valeur trop grande transformerait un accès hors instantané en
// lecture de mémoire voisine, et une valeur trop petite ferait rejeter de la
// géométrie légitime en la comptant comme adresse invalide.
constexpr unsigned kSnapshotBytes = 0x800000u;

// La Voodoo 2 de cette machine. 640x480 est le mode que E05 a mesuré, et le seul
// que ce portage ouvre pour l'instant.
constexpr int kWidth = 640;
constexpr int kHeight = 480;
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

    // Réinitialisé à chaque display list, et non conservé d'une image à l'autre.
    // C'est le contrat du microcode : chaque tâche graphique arrive avec ses
    // propres `DMAOffsets` et sa propre matrice. Conserver l'état ferait
    // dépendre une image de la précédente, et le premier symptôme serait une
    // géométrie juste au démarrage puis dérivante — le pire cas à diagnostiquer.
    dkr_f3d_init(&context_, rdram_snapshot, kSnapshotBytes, &backend_);
    // La disposition de librecomp, entrelacée par XOR-3. Sans ce drapeau le
    // décodeur lirait des opcodes plausibles à des adresses absurdes.
    context_.rdram_native = 1;
    dkr_transform_set_viewport(&context_.transform,
                               static_cast<float>(width_) * 0.5F,
                               -static_cast<float>(height_) * 0.5F,
                               static_cast<float>(width_) * 0.5F,
                               static_cast<float>(height_) * 0.5F);

    // L'adresse est virtuelle côté invité (0x80xxxxxx) ; l'instantané est
    // indexé physiquement.
    (void)dkr_f3d_run(&context_, task->t.data_ptr & 0x00FFFFFFu);

    backend_.present(backend_.self);

    total_commands_ += context_.state.commands;
    total_triangles_ += context_.state.triangles;
    total_emitted_ += context_.state.emitted;
    {
        int i;
        for (i = 0; i < DKR_F3D_REJECT_COUNT_MAX; i++) {
            total_rejects_ += context_.state.rejects[i];
        }
    }

    // Un relevé périodique plutôt qu'une ligne par image : le journal part sur
    // une disquette émulée, et soixante lignes par seconde la saturent.
    //
    // Les trois chiffres se lisent ensemble et c'est leur écart qui renseigne.
    // Des triangles nombreux et zéro émis désignent le découpage ou la culling ;
    // zéro triangle avec des commandes désigne le décodeur ; des rejets qui
    // montent désignent l'adressage, donc la disposition mémoire.
    if (index <= 3 || index % 60 == 0) {
        std::fprintf(stderr,
                     "[gfx] liste=%llu cmd=%lu tri=%lu emis=%lu rejets=%lu\n",
                     static_cast<unsigned long long>(index), total_commands_,
                     total_triangles_, total_emitted_, total_rejects_);
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
