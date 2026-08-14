/* E02-S05 — une sauvegarde produite ici est-elle lisible ailleurs ?
 *
 * Le critere d'acceptation demande l'echange dans les deux sens entre la version
 * moderne et celle de Windows 95. Ce temoin est la moitie *productrice* de
 * l'epreuve : il ecrit une sauvegarde d'aventure non triviale, et la meme source
 * est compilee pour les deux cibles.
 *
 * La moitie *consommatrice* existe deja : `dkr_save_codec_tests` prend un
 * fichier en argument, le decode, le reencode, et exige l'egalite **octet pour
 * octet**. C'est la bonne exigence, et elle est plus forte qu'un simple
 * « le decodage reussit » : elle prend aussi les differences d'encodage, qui
 * sont precisement ce qu'un changement de plate-forme risque d'introduire —
 * boutisme, remplissage, largeur des champs.
 *
 * Le contenu ecrit n'est pas une sauvegarde vierge. Une sauvegarde vierge est
 * surtout faite de zeros, et des zeros survivent a peu pres a n'importe quelle
 * erreur de conversion. On pose donc des valeurs distinctes dans les champs de
 * largeurs differentes, de sorte qu'une erreur se voie.
 */
#include "dkr_save_codec.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    using namespace dkr::runtime::saves::codec;

    const char *path = (argc >= 2) ? argv[1] : "SAVEOUT.BIN";

    SaveImage image{};
    std::string error;
    const std::vector<std::uint8_t> blank = blank_bytes();
    if (!decode(blank, image, &error)) {
        std::fprintf(stderr, "[interchange] la sauvegarde vierge ne se decode pas : %s\n",
                     error.c_str());
        return 1;
    }

    /* Des valeurs choisies pour qu'une erreur de conversion se voie.
     *
     * Les champs de 16 et 32 bits portent des motifs **asymetriques** — 0x1234
     * et non 0x1221 — parce qu'une inversion d'octets sur une valeur symetrique
     * ne se voit pas. C'est le risque principal quand la meme structure est
     * encodee par deux compilateurs differents, et le seul que des zeros ne
     * revelent jamais. */
    image.slots[0].name             = "WIN95";
    image.slots[0].course_status[0] = 3;
    image.slots[0].balloons[0]      = 37;
    image.slots[0].balloons[1]      = 8;
    image.slots[0].wizpig_amulet    = 4;
    image.slots[0].tt_amulet        = 1;
    image.slots[0].trophies         = 0x1234;
    image.slots[0].bosses           = 0x5678;
    image.slots[0].world_flags[0]   = 0x0BAD;
    image.slots[0].cutscenes        = 0x12345678u;
    image.slots[0].keys             = 5;
    image.slots[1].balloons[0]      = 12;

    const std::vector<std::uint8_t> bytes = encode(image);

    FILE *out = std::fopen(path, "wb");
    if (!out) {
        std::fprintf(stderr, "[interchange] ecriture impossible : %s\n", path);
        return 1;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
    /* Sur un support plein, `fwrite` peut rendre moins que demande sans que
       `fclose` ne le dise. Le compte rendu, lui, ne ment pas. */
    const bool closed_cleanly = (std::fclose(out) == 0);
    if (written != bytes.size() || !closed_cleanly) {
        std::fprintf(stderr, "[interchange] ecriture incomplete : %u sur %u octets\n",
                     (unsigned)written, (unsigned)bytes.size());
        return 1;
    }

    std::printf("[interchange] ecrit %u octets dans %s\n",
                (unsigned)bytes.size(), path);
    return 0;
}
