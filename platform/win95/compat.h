/* E01-S03 — couche de compatibilite d'API Windows 95.
 *
 * Ce fichier n'a pas besoin d'etre inclus pour que la couche agisse : les
 * fonctions manquantes sont fournies au lieur, et redirigees vers nos
 * implementations par les pointeurs `__imp__X@n`. Le reste du projet ne s'en
 * apercoit pas, ce qui est le but — aucun autre ticket ne doit avoir a s'en
 * preoccuper.
 *
 * Il expose ce qui doit etre testable ou appelable explicitement.
 *
 * La semantique perdue par chaque contournement est ecrite dans
 * `docs/WIN95-COMPAT.md`. Un contournement dont la difference n'est pas ecrite
 * est un bogue en attente.
 */
#ifndef DKR_WIN95_COMPAT_H
#define DKR_WIN95_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- API que la couche fournit et que les en-tetes masquent --------------- *
 *
 * La toolchain pose `_WIN32_WINNT=0x0400` pour qu'une API posterieure a
 * Windows 95 echoue a la compilation plutot qu'au chargement (E01-S01). Le
 * garde ne fait pas de difference entre une API qu'on utiliserait par
 * inadvertance et une que cette couche fournit : il masque les deux.
 *
 * Il faut donc redeclarer ce que nous implementons. La declaration est
 * conditionnee a la valeur du garde, pour ne pas entrer en conflit sur une
 * cible ou l'en-tete du systeme la fournit deja.
 *
 * Les trois autres — `IsDebuggerPresent`, `SetProcessAffinityMask` et
 * `TryEnterCriticalSection` — sont declarees sans garde par mingw, malgre leur
 * absence de Windows 95 : rien a redeclarer pour elles.
 */
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600)
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
#endif

/* --- Horloge monotone 64 bits -------------------------------------------- *
 *
 * `GetTickCount` revient a zero apres 49,7 jours. Le cas ne se rencontre jamais
 * en test et se rencontre chez un joueur qui laisse sa machine allumee — c'est
 * exactement le genre de defaut qu'on ne trouve pas en le cherchant.
 *
 * La logique d'accumulation est isolee ici sous forme de fonction pure, de sorte
 * que le passage a zero puisse etre simule dans un test au lieu d'etre attendu
 * pendant sept semaines.
 */
typedef struct {
    unsigned long high;   /* nombre de rebouclages observes */
    unsigned long last;   /* derniere valeur 32 bits vue */
} dkr_tick64_state;

/* Avance l'etat avec une lecture 32 bits et rend le compteur 64 bits.
   `now32` est ce que `GetTickCount` a renvoye. */
unsigned long long dkr_tick64_step(dkr_tick64_state *state, unsigned long now32);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_COMPAT_H */
