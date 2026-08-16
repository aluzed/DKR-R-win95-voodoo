#include "glide_renderer.hpp"

#include "game_registration.hpp"

#include "librecomp/game.hpp"

#include <cstdio>
#include <cstring>
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

// --- Lire l'état du jeu, plutôt que de le déduire du rendu -------------------
//
// Toutes les mesures jusqu'ici décrivaient le même état stationnaire — même
// nombre de commandes par liste, même couleur de fond, aucun tri de profondeur.
// Elles disent ce que le jeu dessine, jamais **où il en est**. Continuer à
// perfectionner le rendu d'une image que le jeu n'a peut-être pas l'intention
// de faire évoluer serait mal employer l'effort.
//
// `gGameMode` est la variable que DKR lui-même consulte pour savoir quoi faire.
// Son adresse vient de la carte des symboles du décomp :
//
//     0x801234ec  gGameMode          -1 INTRO, 0 INGAME, 1 MENU, 5 LOCKUP
//     0x800dd394  gLevelLoadTimer
//     0x80121168  gCurrentLevelHeader
//
// `GAMEMODE_LOCKUP` mérite une mention : le jeu s'y met lui-même quand
// `get_lockup_status()` répond vrai, et il affiche alors un écran de plantage.
// S'il y est, aucune correction de rendu n'y changera rien.
//
// **La lecture est native, sans permutation d'octets.** La RDRAM de librecomp
// est entrelacée par XOR-3, et pour un mot de 32 bits aligné l'entrelacement et
// le petit-boutisme de l'hôte s'annulent exactement. Retourner les octets « pour
// corriger le boutisme » est l'erreur déjà commise une fois cette session, sur
// la lecture de `curRDPTask`.
constexpr unsigned kAdrGameMode = 0x1234ECu;
constexpr unsigned kAdrLevelLoadTimer = 0x0DD394u;
constexpr unsigned kAdrLevelHeader = 0x121168u;

int lire_mot(const std::uint8_t* rdram, unsigned adresse) {
    int v = 0;
    std::memcpy(&v, rdram + adresse, sizeof(v));
    return v;
}

// La trace du décodeur, bornée.
//
// Sans elle on lit « un rejet par liste » et l'on ne sait pas lequel : le
// compteur dit qu'il y a un problème, la trace dit lequel. Elle est bornée
// parce que le journal part sur une disquette émulée, et parce que les
// premières occurrences suffisent — un rejet qui se répète six cents fois se
// diagnostique sur la première.
// **Deux budgets, pas un.**
//
// Un budget unique a déjà coûté un aller-retour : les commandes différées, bien
// plus nombreuses, l'épuisaient avant qu'un seul rejet n'ait été écrit. Le
// journal montrait alors « 589 rejets d'opcode » sans dire lequel — le compteur
// disait qu'il y avait un problème, et la trace, censée dire lequel, avait été
// dépensée sur du bruit.
//
// Le rejet est ce qu'on cherche ; le reste est du contexte. Ils ne peuvent pas
// puiser au même seau.
unsigned g_trace_rejets = 24;
unsigned g_trace_contexte = 24;

bool commence_par(const char* ligne, const char* prefixe) {
    while (*prefixe != '\0') {
        if (*ligne != *prefixe) { return false; }
        ligne++;
        prefixe++;
    }
    return true;
}

void trace_decodeur(void*, const char* ligne) {
    unsigned* seau = commence_par(ligne, "REJET") ? &g_trace_rejets : &g_trace_contexte;
    if (*seau == 0) { return; }
    (*seau)--;
    std::fprintf(stderr, "[gfx][f3d] %s\n", ligne);
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

    // Réinitialisé à chaque display list, et non conservé d'une image à l'autre.
    // C'est le contrat du microcode : chaque tâche graphique arrive avec ses
    // propres `DMAOffsets` et sa propre matrice. Conserver l'état ferait
    // dépendre une image de la précédente, et le premier symptôme serait une
    // géométrie juste au démarrage puis dérivante — le pire cas à diagnostiquer.
    dkr_f3d_init(&context_, rdram_snapshot, kSnapshotBytes, &backend_);
    // La disposition de librecomp, entrelacée par XOR-3. Sans ce drapeau le
    // décodeur lirait des opcodes plausibles à des adresses absurdes.
    context_.rdram_native = 1;
    context_.trace = trace_decodeur;
    // La résolution réellement ouverte : le décodeur en a besoin pour porter le
    // tampon du jeu (320 de large) à l'écran, aussi bien pour les rectangles 2D
    // que pour la fenêtre d'affichage 3D.
    context_.screen_width = static_cast<unsigned>(width_);
    context_.screen_height = static_cast<unsigned>(height_);

    // **Une liste entière, vidée une seule fois.**
    //
    // Les compteurs ont mené jusqu'ici puis se sont tus : 70 commandes par
    // liste, constant depuis la liste 300, deux remplissages et rien d'autre.
    // Un chiffre stable ne dit plus rien de ce que le jeu fabrique ; il faut
    // voir la liste.
    //
    // Le choix de la 300e n'est pas arbitraire : c'est à partir de là que le
    // débit se stabilise, donc la première qui décrit l'état où le jeu reste.
    // Vider la première donnerait la séquence d'initialisation, qui n'est pas
    // celle où il est bloqué.
    if (index == 300) {
        g_trace_contexte = 400;
        std::fprintf(stderr, "[gfx] --- liste 300, contenu integral ---\n");
    }
    dkr_transform_set_viewport(&context_.transform,
                               static_cast<float>(width_) * 0.5F,
                               -static_cast<float>(height_) * 0.5F,
                               static_cast<float>(width_) * 0.5F,
                               static_cast<float>(height_) * 0.5F);

    // L'adresse est virtuelle côté invité (0x80xxxxxx) ; l'instantané est
    // indexé physiquement.
    (void)dkr_f3d_run(&context_, task->t.data_ptr & 0x00FFFFFFu);

    backend_.present(backend_.self);

    for (int i = 0; i < DKR_F3D_REJECT_COUNT_MAX; i++) {
        rejects_by_kind_[i] += context_.state.rejects[i];
    }
    for (int i = 0; i < 256; i++) { opcodes_[i] += context_.state.opcodes[i]; }
    total_rects_ += context_.state.rects;
    total_viewports_ += context_.state.viewports;
    total_tex_chargees_ += context_.state.textures_chargees;
    total_tex_reutilisees_ += context_.state.textures_reutilisees;
    total_tex_refusees_ += context_.state.textures_refusees;
    total_tex_remplies_ += context_.state.textures_remplies;
    total_emis_texture_ += context_.state.emis_avec_texture;
    for (int i = 0; i < 4; i++) {
        aire_[i] += context_.state.aire[i];
        profondeur_[i] += context_.state.emis_par_profondeur[i];
    }
    for (int i = 0; i < DKR_COMBINE_COUNT; i++) {
        emis_par_combine_[i] += context_.state.emis_par_combine[i];
    }
    total_tex_proportions_ += context_.state.textures_hors_proportions;
    total_tex_inconnues_ += context_.state.textures.non_prises_en_charge;
    total_tex_hors_ += context_.state.textures.hors_rdram;
    total_etats_ += context_.state.etats_appliques;
    total_comb_connus_ += context_.state.combineurs_connus;
    total_comb_inconnus_ += context_.state.combineurs_inconnus;
    total_approches_ += context_.state.etats_approches;
    total_fill_hors_cycle_ += context_.state.fill_hors_cycle;
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
        // Le total de rejets ne dit pas quoi corriger : une adresse hors RDRAM
        // accuse l'adressage, un opcode inconnu accuse le décodage, un index de
        // sommet accuse une commande manquée en amont. Les compter séparément
        // est ce qui transforme « ça rejette » en une piste.
        std::fprintf(stderr,
                     "[gfx]   differees=%lu | adresse=%lu nombre=%lu index=%lu "
                     "profondeur=%lu opcode=%lu\n",
                     total_deferred_,
                     rejects_by_kind_[DKR_F3D_REJECT_ADDRESS],
                     rejects_by_kind_[DKR_F3D_REJECT_COUNT],
                     rejects_by_kind_[DKR_F3D_REJECT_INDEX],
                     rejects_by_kind_[DKR_F3D_REJECT_DEPTH],
                     rejects_by_kind_[DKR_F3D_REJECT_OPCODE]);
        // Les huit opcodes les plus fréquents, par ordre décroissant. Huit
        // suffisent : la distribution est très inégale, et ce qu'on cherche est
        // ce qui domine l'image, pas la queue.
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
        // Les ordres de dessin, nommés et cherchés explicitement.
        //
        // Les huit premiers de l'histogramme sont tous de l'état RDP, ce qui
        // laisse une question ouverte que le classement ne tranche pas : y a-t-il
        // *le moindre* ordre de dessin dans ces images, ou aucun ? Un opcode
        // absent ne figure dans aucun classement, et « absent du top huit » se
        // lit trop facilement comme « rare » alors qu'il peut valoir zéro.
        //
        // Zéro partout dirait que la séquence de démarrage ne dessine rien du
        // tout et que l'on regarde l'écran de chargement du jeu. Des rectangles
        // sans triangles désignerait le chemin 2D comme seul travail restant.
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
                     total_etats_, total_approches_, total_fill_hors_cycle_,
                     static_cast<unsigned>(context_.state.cycle_courant),
                     total_viewports_);
        // Les textures. `chargees` contre `reutilisees` dit si le cache tient —
        // sans lui on reconvertirait la meme texture des milliers de fois par
        // image, ce qui suffirait a rendre le portage injouable. `inconnues`
        // compte les formats indexes, refuses faute de palette : ils sortent en
        // surfaces sans texture plutot qu'en couleurs arbitraires.
        std::fprintf(stderr,
                     "[gfx]   textures: chargees=%lu reutilisees=%lu "
                     "refusees-tmu=%lu format-inconnu=%lu hors-rdram=%lu\n",
                     total_tex_chargees_, total_tex_reutilisees_,
                     total_tex_refusees_, total_tex_inconnues_, total_tex_hors_);
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
                     total_tex_remplies_, total_tex_proportions_);
        // Les coordonnées normalisées. Un voisinage de [0,1] confirme le format
        // 10.5 et la largeur employée ; des milliers le réfutent.
        std::fprintf(stderr,
                     "[gfx]   emis: avec-texture=%lu | shade=%lu texel=%lu "
                     "texel*shade=%lu texel*shade+a=%lu\n",
                     total_emis_texture_, emis_par_combine_[0],
                     emis_par_combine_[1], emis_par_combine_[2],
                     emis_par_combine_[3]);
        std::fprintf(stderr,
                     "[gfx]   aires: <1px=%lu <100px=%lu <10000px=%lu "
                     ">=10000px=%lu\n",
                     aire_[0], aire_[1], aire_[2], aire_[3]);
        std::fprintf(stderr,
                     "[gfx]   profondeur: mode0=%lu mode1=%lu mode2=%lu mode3=%lu\n",
                     profondeur_[0], profondeur_[1], profondeur_[2],
                     profondeur_[3]);
        {
            char ligne[128];
            std::size_t ecrit = 0;
            unsigned i;
            ligne[0] = '\0';
            for (i = 0; i < context_.state.cles_inconnues_n && ecrit < 100; i++) {
                ecrit += static_cast<std::size_t>(std::snprintf(
                    ligne + ecrit, sizeof(ligne) - ecrit, " %08X",
                    static_cast<unsigned>(context_.state.cles_inconnues[i])));
            }
            std::fprintf(stderr,
                         "[gfx]   combineurs: repertories=%lu inconnus=%lu%s%s\n",
                         total_comb_connus_, total_comb_inconnus_,
                         (ligne[0] != '\0') ? " cles:" : "", ligne);
            // La composition, sous la forme (a,b,c,d) que `gDPSetCombineLERP`
            // prend — c'est celle des macros G_CC_*, donc celle qui permet de
            // nommer la configuration et de l'ajouter à la table.
            for (i = 0; i < context_.state.cles_inconnues_n; i++) {
                const dkr_combiner& k = context_.state.compo_inconnues[i];
                std::fprintf(stderr,
                             "[gfx]     %08X cycle=%u rgb0=(%u,%u,%u,%u) "
                             "a0=(%u,%u,%u,%u) rgb1=(%u,%u,%u,%u) "
                             "a1=(%u,%u,%u,%u)\n",
                             static_cast<unsigned>(context_.state.cles_inconnues[i]),
                             context_.state.cycle_inconnu[i],
                             k.rgb[0].a, k.rgb[0].b, k.rgb[0].c, k.rgb[0].d,
                             k.alpha[0].a, k.alpha[0].b, k.alpha[0].c, k.alpha[0].d,
                             k.rgb[1].a, k.rgb[1].b, k.rgb[1].c, k.rgb[1].d,
                             k.alpha[1].a, k.alpha[1].b, k.alpha[1].c, k.alpha[1].d);
            }
        }
        // L'état du jeu, lu chez lui. C'est la seule mesure de cette série qui
        // ne parle pas du rendu.
        {
            const int mode = lire_mot(rdram_snapshot, kAdrGameMode);
            static const char* noms[] = { "INGAME", "MENU", "UNUSED2",
                                          "UNUSED3", "UNUSED4", "LOCKUP" };
            const char* nom = (mode == -1) ? "INTRO"
                            : (mode >= 0 && mode <= 5) ? noms[mode] : "?";
            std::fprintf(stderr,
                         "[jeu] gGameMode=%d (%s) chargement=%d niveau=0x%08X\n",
                         mode, nom, lire_mot(rdram_snapshot, kAdrLevelLoadTimer),
                         static_cast<unsigned>(
                             lire_mot(rdram_snapshot, kAdrLevelHeader)));
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
