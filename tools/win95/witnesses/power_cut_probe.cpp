/* E02-S05 — la coupure de courant, provoquee plutot que simulee.
 *
 * Ce que la sequence d'ecriture durable promet n'est pas « on ne perd jamais la
 * derniere ecriture » mais **« on ne perd jamais une sauvegarde valide »**. La
 * fenetre est assumee : entre le renommage de SAUVE.DAT vers SAUVE.BAK et celui
 * de SAUVE.TMP vers SAUVE.DAT, le fichier final n'existe pas.
 *
 * Jusqu'ici la promesse etait raisonnee et eprouvee par simulation. Sur une
 * machine emulee, la vraie coupure est a portee : `kill -9` sur l'emulateur
 * emporte le cache disque de l'invite comme le ferait une prise arrachee.
 *
 * Deux modes :
 *
 *   ecrire    boucle sans fin, chaque tour ecrivant une sauvegarde numerotee
 *   verifier  relit apres redemarrage et dit ce qui a survecu
 *
 * Le contenu porte un numero de tour et une somme de controle, de sorte que
 * « complet » se distingue de « tronque ». Sans cela on ne saurait pas si le
 * fichier retrouve est utilisable ou seulement present — et c'est toute la
 * difference que la sequence pretend garantir.
 *
 * Une reserve sur la severite : `kill -9` emporte aussi ce que l'emulateur
 * gardait dans le cache de l'hote, que le materiel reel aurait deja ecrit. Le
 * protocole est donc **au moins aussi dur** qu'une coupure veritable, jamais
 * plus doux. C'est le bon sens de l'erreur.
 */
#include "fileio.h"

#include <stdio.h>
#include <string.h>

#define PAYLOAD 512

/* Somme de controle simple ; il ne s'agit pas de resister a une falsification
   mais de distinguer un fichier complet d'un fichier tronque. */
static unsigned long checksum(const unsigned char *p, size_t n)
{
    unsigned long sum = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        sum = (sum << 1) ^ (sum >> 31) ^ p[i];
    }
    return sum;
}

static void fill(unsigned char *buf, unsigned long round)
{
    size_t i;
    memset(buf, 0, PAYLOAD);
    /* Le numero de tour en tete, en octets explicites : la sonde doit se relire
       elle-meme sans dependre du boutisme du compilateur. */
    buf[0] = (unsigned char)(round & 0xFF);
    buf[1] = (unsigned char)((round >> 8) & 0xFF);
    buf[2] = (unsigned char)((round >> 16) & 0xFF);
    buf[3] = (unsigned char)((round >> 24) & 0xFF);
    for (i = 8; i < PAYLOAD; i++) {
        buf[i] = (unsigned char)((round + i) & 0xFF);
    }
    {
        const unsigned long c = checksum(buf + 8, PAYLOAD - 8);
        buf[4] = (unsigned char)(c & 0xFF);
        buf[5] = (unsigned char)((c >> 8) & 0xFF);
        buf[6] = (unsigned char)((c >> 16) & 0xFF);
        buf[7] = (unsigned char)((c >> 24) & 0xFF);
    }
}

static int payload_is_intact(const unsigned char *buf, size_t n,
                             unsigned long *round_out)
{
    unsigned long stored, computed;
    if (n != PAYLOAD) {
        return 0;
    }
    stored = (unsigned long)buf[4] | ((unsigned long)buf[5] << 8) |
             ((unsigned long)buf[6] << 16) | ((unsigned long)buf[7] << 24);
    computed = checksum(buf + 8, PAYLOAD - 8);
    if (round_out) {
        *round_out = (unsigned long)buf[0] | ((unsigned long)buf[1] << 8) |
                     ((unsigned long)buf[2] << 16) | ((unsigned long)buf[3] << 24);
    }
    return stored == computed;
}

int main(int argc, char **argv)
{
    const char *mode = (argc >= 2) ? argv[1] : "ecrire";
    const char *path = (argc >= 3) ? argv[2] : "D:\\COUPURE.DAT";
    unsigned char buf[PAYLOAD];

    if (strcmp(mode, "verifier") == 0) {
        size_t          got = 0;
        int             from_backup = 0;
        unsigned long   round = 0;
        dkr_file_result r;
        FILE           *log = fopen("D:\\COUPURE.TXT", "w");

        r = dkr_file_read_durable(path, buf, sizeof(buf), &got, &from_backup);

        {
            const int intact = (r == DKR_FILE_OK) &&
                               payload_is_intact(buf, got, &round);
            const char *verdict =
                (r != DKR_FILE_OK) ? "AUCUNE sauvegarde relisible"
                : intact ? (from_backup ? "sauvegarde valide, depuis la copie de secours"
                                        : "sauvegarde valide, fichier principal")
                         : "fichier present mais TRONQUE";
            printf("  code de lecture    : %d (%s)\n", (int)r, dkr_file_result_text(r));
            printf("  octets relus       : %u\n", (unsigned)got);
            printf("  numero de tour     : %lu\n", round);
            printf("  verdict            : %s\n", verdict);
            if (log) {
                fprintf(log, "  code de lecture    : %d (%s)\n",
                        (int)r, dkr_file_result_text(r));
                fprintf(log, "  octets relus       : %u\n", (unsigned)got);
                fprintf(log, "  numero de tour     : %lu\n", round);
                fprintf(log, "  verdict            : %s\n", verdict);
                fclose(log);
            }
            return intact ? 0 : 1;
        }
    }

    /* Mode ecriture : sans fin, jusqu'a ce que la machine s'arrete. */
    {
        unsigned long round = 0;
        printf("Ecriture en boucle dans %s — couper la machine quand on veut.\n", path);
        for (;;) {
            fill(buf, round);
            if (dkr_file_write_durable(path, buf, sizeof(buf)) != DKR_FILE_OK) {
                printf("  echec au tour %lu\n", round);
                return 2;
            }
            round++;
            if ((round % 25) == 0) {
                printf("  %lu tours\n", round);
                fflush(stdout);
            }
        }
    }
}
