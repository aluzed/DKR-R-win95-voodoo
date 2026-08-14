/* E02-S05 — que rend l'ecriture durable quand le support est plein ?
 *
 * Le ticket demandait des codes d'erreur distincts et exploitables : « disque
 * plein » et « support protege » n'appellent pas le meme geste de la part du
 * joueur. La suite d'epreuve verifiait jusqu'ici que les codes sont *distincts*
 * et portent un texte — ce qui est necessaire et pas suffisant. Rien ne prouvait
 * qu'un disque reellement plein rende `DKR_FILE_ERR_NO_SPACE` plutot que
 * `DKR_FILE_ERR_IO`.
 *
 * C'est ce que cette sonde etablit, et elle demande un volume qu'on puisse
 * remplir : une disquette de 1,44 Mo montee en A:, remplie a l'avance depuis
 * l'hote. Le disque dur de transfert a un demi-gigaoctet de libre, ce qui rend
 * l'exercice impraticable par ce chemin.
 *
 * Deux precautions valent d'etre dites :
 *
 *   - L'ecriture visee est **plus grande que le volume entier**, et non
 *     seulement que l'espace restant. Une ecriture qui tiendrait tout juste ne
 *     prouverait rien de reproductible : la place libre depend de ce qui traine
 *     sur le support.
 *
 *   - Le code rendu est imprime **avec son texte**, parce que c'est le texte que
 *     le joueur lira. Un code juste accompagne d'un message faux serait un
 *     progres illusoire.
 */
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : "A:\\PLEIN.DAT";
    /* 2 Mo : au-dela de la capacite d'une disquette 3,5" haute densite, quoi
       qu'elle contienne deja. */
    const size_t size = 2u * 1024u * 1024u;
    unsigned char *buffer;
    dkr_file_result r;
    FILE *log;

    log = fopen("D:\\DISKFULL.TXT", "w");

    buffer = (unsigned char *)malloc(size);
    if (!buffer) {
        printf("  memoire insuffisante pour la sonde\n");
        if (log) { fprintf(log, "  memoire insuffisante pour la sonde\n"); fclose(log); }
        return 2;
    }
    memset(buffer, 0xA5, size);

    r = dkr_file_write_durable(path, buffer, size);
    free(buffer);

    printf("  cible              : %s\n", path);
    printf("  taille demandee    : %u octets\n", (unsigned)size);
    printf("  code rendu         : %d\n", (int)r);
    printf("  texte              : %s\n", dkr_file_result_text(r));
    printf("  verdict            : %s\n",
           (r == DKR_FILE_ERR_NO_SPACE) ? "DISQUE PLEIN, correctement nomme"
                                        : "PAS le code disque plein");
    if (log) {
        fprintf(log, "  cible              : %s\n", path);
        fprintf(log, "  taille demandee    : %u octets\n", (unsigned)size);
        fprintf(log, "  code rendu         : %d\n", (int)r);
        fprintf(log, "  texte              : %s\n", dkr_file_result_text(r));
        fprintf(log, "  verdict            : %s\n",
                (r == DKR_FILE_ERR_NO_SPACE) ? "DISQUE PLEIN, correctement nomme"
                                             : "PAS le code disque plein");
        fclose(log);
    }
    return (r == DKR_FILE_ERR_NO_SPACE) ? 0 : 1;
}
