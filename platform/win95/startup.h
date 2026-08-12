/* E01-S03 — demarrage de la cible Windows 95.
 *
 * A appeler en premiere ligne de `main`. Trois choses que personne d'autre ne
 * fera, et dont l'absence coute cher :
 *
 *  1. Un **journal de demarrage dans un fichier**. Il n'y a pas de console
 *     utilisable sur la machine cible : un jeu plein ecran qui meurt avant son
 *     premier affichage ne laisse rien a lire. Le journal est ecrit a cote de
 *     l'executable et vide a chaque ligne, de sorte que la derniere ligne
 *     survive au plantage qui l'a interrompue.
 *
 *  2. Un **filtre d'exceptions structurees**. Sans lui, une instruction
 *     invalide ou un acces fautif produit une boite de dialogue de Windows 95
 *     qui ne nomme rien d'exploitable. Avec lui, le code et l'adresse partent
 *     dans le journal.
 *
 *  3. Un **controle de version**. Le plancher retenu est Windows 95
 *     (ADR 0002) ; refuser proprement vaut mieux que planter sur une API
 *     absente, et infiniment mieux que de planter au hasard plus tard.
 */
#ifndef DKR_WIN95_STARTUP_H
#define DKR_WIN95_STARTUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Codes de retour, pour que `main` puisse rendre autre chose que 1. */
enum {
    DKR_WIN95_STARTUP_OK          = 0,
    DKR_WIN95_STARTUP_TOO_OLD     = 2,   /* systeme anterieur au plancher */
    DKR_WIN95_STARTUP_NO_LOG      = 3    /* journal impossible a ouvrir */
};

/* Prepare le journal, le filtre d'exceptions et verifie la version du systeme.
   Renvoie DKR_WIN95_STARTUP_OK, ou un code d'erreur apres avoir affiche un
   message comprehensible. `app_name` apparait dans le journal et les boites de
   dialogue. */
int dkr_win95_startup(const char *app_name);

/* Ecrit une ligne dans le journal de demarrage. Sans effet avant
   `dkr_win95_startup`. Le fichier est vide apres chaque ligne : une ligne ecrite
   est une ligne qui survivra au plantage suivant. */
void dkr_win95_log(const char *message);

/* Idem, avec un entier a la suite — de quoi tracer un code d'erreur sans
   embarquer de printf. */
void dkr_win95_log_num(const char *message, long value);

/* Ferme le journal. Facultatif : le systeme le fera. */
void dkr_win95_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_STARTUP_H */
