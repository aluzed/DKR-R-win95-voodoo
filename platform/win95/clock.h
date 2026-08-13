/* E02-S03 — base de temps monotone pour Windows 95.
 *
 * Toute la simulation en depend : cadence des images, chronometrage des courses,
 * temporisation de l'audio. Une base qui derive lentement ne casse rien de
 * visible et fausse tout.
 *
 * ## Ce que la machine offre reellement
 *
 * Mesure sur la cible, et non tableau de compatibilite — le releve complet est
 * dans `docs/research/win95-clock.md` :
 *
 *   source                    resolution   monotonie      cout relatif
 *   GetTickCount                   9 ms    —              1
 *   timeGetTime                    1 ms    0 recul        ~135
 *   QueryPerformanceCounter      4,19 us   0 recul        ~104
 *
 * Deux resultats contredisent ce que le ticket supposait, et changent la
 * conception :
 *
 *  1. **`timeBeginPeriod(1)` ne sert a rien ici.** `timeGetTime` rend deja la
 *     milliseconde avant tout reglage. L'appel est neanmoins fait et relache,
 *     parce que rien ne garantit qu'il en aille de meme sur une autre machine,
 *     et parce qu'un reglage laisse en place degrade tout le systeme jusqu'au
 *     redemarrage.
 *
 *  2. **`GetTickCount` est deux ordres de grandeur moins chere** que les deux
 *     autres. Elle lit une variable en memoire partagee ; `QueryPerformanceCounter`
 *     lit le PIT par des acces d'entree-sortie, et `timeGetTime` traverse
 *     `winmm`. La resolution se paie, et le chiffre est a verser au budget de
 *     E08-S01.
 *
 * ## La frequence dit d'ou vient le compteur
 *
 * `QueryPerformanceFrequency` rend **1 193 180 Hz**, c'est-a-dire la frequence
 * du PIT 8254. Ce n'est donc pas le compteur de cycles du processeur, et il en
 * decoule un fait qu'il vaut mieux connaitre avant qu'apres : **les 32 bits de
 * poids faible de ce compteur rebouclent en exactement 60 minutes.**
 *
 * L'API rend 64 bits et Windows 95 etend le compteur ; cette couche s'appuie
 * donc sur la valeur complete. Mais le repli `timeGetTime`, lui, est un
 * compteur de 32 bits qui reboucle apres 49,7 jours, et il est accumule — par
 * la meme fonction pure que `GetTickCount64` de E01-S03, dont c'est exactement
 * le probleme.
 */
#ifndef DKR_WIN95_CLOCK_H
#define DKR_WIN95_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* La source retenue au lancement. Rendue pour que le journal de demarrage la
   nomme : quand une machine se comporte etrangement, savoir sur quelle horloge
   elle tourne est la premiere question. */
typedef enum {
    DKR_CLOCK_SOURCE_NONE = 0,
    DKR_CLOCK_SOURCE_QPC,          /* QueryPerformanceCounter */
    DKR_CLOCK_SOURCE_TIMEGETTIME   /* repli */
} dkr_clock_source;

/* Selectionne et **valide** la source. Une source qui s'annonce et se comporte
   mal est ecartee au profit du repli, plutot que de produire un comportement
   erratique en jeu. Rend 1 si une source utilisable a ete retenue. */
int              dkr_clock_init(void);
void             dkr_clock_shutdown(void);   /* relache timeBeginPeriod */

dkr_clock_source dkr_clock_source_in_use(void);
const char      *dkr_clock_source_name(void);

/* Pas par seconde de la source retenue. */
unsigned long long dkr_clock_frequency(void);

/* Temps ecoule depuis `dkr_clock_init`, en pas de la source. Monotone. */
unsigned long long dkr_clock_now(void);

/* Le meme, en microsecondes. */
unsigned long long dkr_clock_now_us(void);

/* --- Compteur de cycles du VR4300 ----------------------------------------- *
 *
 * DKR mesure le temps par ce compteur, qui avance a 46,875 MHz — la moitie de
 * la frequence du processeur de la N64 — **independamment** de la frequence de
 * l'hote. C'est ce rapport qui doit etre exact ; une derive lente y fausserait
 * tous les chronometrages de course sans rien casser de visible.
 */
#define DKR_VR4300_COUNTER_HZ 46875000ULL

unsigned long long dkr_clock_vr4300_count(void);

/* La conversion, isolee en fonction pure pour etre testable sur l'hote sans
   Windows — et pour que son absence de debordement se demontre plutot que se
   suppose.
 *
 * L'ecriture naive `ticks * 46875000 / frequency` deborde des que `ticks`
 * depasse 2^63 / 46 875 000, soit environ 46 heures de jeu. On separe donc le
 * quotient du reste : le reste est plus petit que la frequence, et son produit
 * tient largement. */
unsigned long long dkr_clock_ticks_to_vr4300(unsigned long long ticks,
                                             unsigned long long frequency);

/* --- Accumulation d'une source de 32 bits --------------------------------- *
 *
 * Le repli `timeGetTime` reboucle apres 49,7 jours. La logique d'accumulation
 * est celle de `dkr_tick64_step` (E01-S03) : elle est reprise et non recopiee,
 * parce qu'un second exemplaire du meme raisonnement finit toujours par diverger
 * du premier. Voir `platform/win95/tick64.c`.
 */

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_CLOCK_H */
