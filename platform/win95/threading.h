/* E02-S01 — fils d'execution et synchronisation pour Windows 95.
 *
 * Interface minimale batie uniquement sur des API que Windows 95 exporte *et
 * implemente*, sur laquelle `ultramodern` sera repose en E02-S02.
 *
 * Le perimetre n'est pas deduit d'un modele generique : il est releve dans le
 * code d'`ultramodern`, parce que tout surplus se paie en travail de portage.
 * Ce qui y est reellement utilise, et rien d'autre :
 *
 *   std::thread                  x12   creation, jonction, detachement
 *   std::mutex + lock_guard      x5    exclusion mutuelle, non recursive
 *   LightweightSemaphore         xN    *le* primitif de blocage du planificateur
 *   thread_local                 x3    threads.cpp : deux drapeaux, un pointeur
 *   this_thread::sleep_for/until       temporisation
 *   set_native_thread_priority         5 niveaux, purement indicatifs
 *
 * Deux absences meritent d'etre notees, parce qu'elles reduisent le ticket :
 *
 *   - **Aucune variable de condition.** `ultramodern` n'en declare pas une
 *     seule. Son attente conditionnelle est un *semaphore de comptage*, et rien
 *     d'autre. La reimplementation delicate que le ticket redoutait — evenements
 *     par attendeur plus compteur protege, ou l'on perd des reveils — est donc
 *     sans objet. On fournit le semaphore, qui est ce qui est demande.
 *
 *   - **Aucune correspondance de priorite N64 -> Win32.** L'ordre de priorite
 *     de la N64 est tenu par la file d'attente logicielle d'`ultramodern`
 *     (`thread_queue_insert` insere en ordre de `OSPri`), et un seul fil de jeu
 *     court a la fois. Le systeme hote n'arbitre jamais entre deux fils de jeu.
 *     Voir `docs/WIN95-THREADING.md`.
 *
 * La semantique perdue par chaque primitive est ecrite dans
 * `docs/WIN95-THREADING.md`. Un contournement dont la difference n'est pas
 * ecrite est un bogue en attente.
 */
#ifndef DKR_WIN95_THREADING_H
#define DKR_WIN95_THREADING_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- Mise en service ------------------------------------------------------ *
 *
 * A appeler une fois, depuis le fil principal, avant tout autre appel de cette
 * couche — en pratique juste apres `dkr_win95_startup`. Elle reserve l'unique
 * emplacement TLS (voir plus bas) et prend note du fil appelant.
 */
int  dkr_threading_init(void);
void dkr_threading_shutdown(void);

/* --- Fils ----------------------------------------------------------------- */

typedef struct dkr_thread dkr_thread;
typedef void (*dkr_thread_fn)(void *arg);

/* Demarre un fil. `stack_bytes` a 0 laisse la taille par defaut du systeme.
   Rend NULL en cas d'echec. Le fil rendu doit etre soit joint, soit relache. */
dkr_thread *dkr_thread_start(dkr_thread_fn fn, void *arg, unsigned long stack_bytes);

/* Attend la fin du fil, puis libere le descripteur. Rend 1 en cas de succes. */
int  dkr_thread_join(dkr_thread *t);

/* Libere le descripteur sans attendre. Le fil continue. Equivalent de
   `std::thread::detach`.

   Peut etre appelee immediatement apres `dkr_thread_start`, y compris avant que
   le fil n'ait execute sa premiere instruction — ce qui, sur un monoprocesseur,
   est le cas ordinaire et non le cas rare. Le fil cree ne partage aucune
   allocation avec ce descripteur, precisement pour cela. */
void dkr_thread_release(dkr_thread *t);

/* Identifiant du fil courant. Jamais 0 pour un fil vivant. */
unsigned long dkr_thread_id(void);

/* --- Priorites ------------------------------------------------------------ *
 *
 * Ces cinq niveaux reproduisent `ultramodern::ThreadPriority` a l'identique, y
 * compris l'ordre, pour que E02-S02 n'ait qu'une conversion triviale a ecrire.
 *
 * Ils ne portent **pas** les priorites N64 : voir l'en-tete de ce fichier.
 */
typedef enum {
    DKR_THREAD_PRIORITY_LOW = 0,
    DKR_THREAD_PRIORITY_NORMAL,
    DKR_THREAD_PRIORITY_HIGH,
    DKR_THREAD_PRIORITY_VERY_HIGH,
    DKR_THREAD_PRIORITY_CRITICAL
} dkr_thread_priority;

/* La correspondance, isolee en fonction pure pour etre testable sur l'hote sans
   Windows. Rend la constante `THREAD_PRIORITY_*` correspondante, ou
   DKR_THREAD_PRIORITY_INVALID pour une entree hors domaine. */
#define DKR_THREAD_PRIORITY_INVALID (-32768)
int  dkr_thread_priority_to_win32(int priority);

/* Applique la priorite au fil courant. Sans effet mesurable sur l'ordre des
   fils de jeu ; utile pour les fils d'infrastructure. */
void dkr_thread_set_priority(dkr_thread_priority priority);

/* --- Temporisation -------------------------------------------------------- */

void dkr_sleep_ms(unsigned long ms);
void dkr_yield(void);

/* --- Exclusion mutuelle --------------------------------------------------- *
 *
 * Bati sur CRITICAL_SECTION — celle de `platform/win95/compat.c`, puisque
 * E01-S03 fournit les cinq fonctions et possede donc la structure.
 *
 * **Non recursif, et verifie.** Les sections critiques de Win32 sont
 * recursives, `std::mutex` ne l'est pas. Un code qui comptait sur l'interblocage
 * d'un `std::mutex` reentrant pour reveler un defaut ne le revelerait plus, et
 * le defaut passerait en production. Cette couche retablit la propriete : une
 * reentrance est detectee et signalee immediatement, au lieu de s'interbloquer
 * silencieusement. Le diagnostic est meilleur que celui de `std::mutex`, pour
 * un cout de deux instructions.
 *
 * La taille reservee est verifiee contre `sizeof(CRITICAL_SECTION)` par un
 * `static_assert` dans `threading.cpp` : l'en-tete n'a pas a inclure windows.h.
 */
typedef struct {
    void          *reserved[16];  /* CRITICAL_SECTION, ou son equivalent hote */
    unsigned long  owner;         /* identifiant du proprietaire, 0 si libre */
    int            initialised;
} dkr_mutex;

int  dkr_mutex_init(dkr_mutex *m);
void dkr_mutex_destroy(dkr_mutex *m);
void dkr_mutex_lock(dkr_mutex *m);
void dkr_mutex_unlock(dkr_mutex *m);

/* Rend 1 si le verrou a ete pris, 0 sinon. Ne signale pas la reentrance : un
   appelant de `try` a deja prevu l'echec, on lui rend simplement 0. */
int  dkr_mutex_try_lock(dkr_mutex *m);

/* --- Semaphore de comptage ------------------------------------------------ *
 *
 * C'est *le* primitif de blocage du planificateur d'`ultramodern` : chaque fil
 * de jeu dort sur `running.wait()` et est reveille par `running.signal()`.
 *
 * Semantique de reveil, a comparer a ce qu'`ultramodern` suppose :
 *
 *   - `signal(n)` reveille **exactement n** attendeurs, jamais plus.
 *   - Un `signal` qui precede le `wait` n'est **pas perdu** : il est compte.
 *     C'est la propriete dont depend le demarrage des fils de jeu, ou le
 *     `signal` du fil createur peut devancer le `wait` du fil cree.
 *   - **L'ordre de reveil n'est pas garanti.** Windows 95 ne promet pas le
 *     FIFO sur un semaphore. `ultramodern` n'en a pas besoin : chacun de ses
 *     semaphores n'a **qu'un seul attendeur possible** — le fil proprietaire du
 *     contexte — de sorte que la question ne se pose pas.
 *   - Pas de reveil intempestif : `wait` ne rend 1 que sur un jeton consomme.
 */
typedef struct {
    void *handle;
} dkr_sem;

int  dkr_sem_init(dkr_sem *s, long initial_count);
void dkr_sem_destroy(dkr_sem *s);

/* Bloque jusqu'a obtenir un jeton. Rend 1 en cas de succes, 0 si le semaphore
   est invalide — jamais un retour silencieux sans jeton. */
int  dkr_sem_wait(dkr_sem *s);

/* Rend 1 si un jeton a ete pris avant l'echeance, 0 sinon. */
int  dkr_sem_wait_timeout(dkr_sem *s, unsigned long ms);

/* Rend 1 si un jeton etait disponible, 0 sinon. Ne bloque jamais. */
int  dkr_sem_try_wait(dkr_sem *s);

/* Depose `count` jetons. Rend 1 en cas de succes. */
int  dkr_sem_signal(dkr_sem *s, long count);

/* --- Evenement a reinitialisation manuelle -------------------------------- *
 *
 * Ce que le semaphore ne sait pas exprimer : reveiller **tous** les attendeurs
 * d'un coup, et rester ouvert pour ceux qui arriveront apres. C'est la forme
 * juste d'un signal « une fois pour toutes » — fin d'initialisation, demande
 * d'arret — la ou un semaphore obligerait a connaitre le nombre d'attendeurs.
 */
typedef struct {
    void *handle;
} dkr_event;

int  dkr_event_init(dkr_event *e, int initially_set);
void dkr_event_destroy(dkr_event *e);
void dkr_event_set(dkr_event *e);
void dkr_event_reset(dkr_event *e);
int  dkr_event_wait(dkr_event *e);
int  dkr_event_wait_timeout(dkr_event *e, unsigned long ms);

/* --- Variables locales au fil --------------------------------------------- *
 *
 * Windows 95 n'offre que 64 emplacements TLS pour tout le processus, et
 * libstdc++ comme winpthreads en consomment deja. Cette couche n'en prend donc
 * **qu'un seul**, qui designe un tableau de pointeurs : le nombre de variables
 * par fil devient une affaire de constante et non de ressource systeme.
 *
 * `ultramodern` en utilise trois (`is_entrypoint_thread`, `is_game_thread`,
 * `thread_self`). La marge est deliberement courte : ce n'est pas un magasin
 * general, et chaque nouvel emplacement doit se justifier.
 */
#define DKR_TLS_SLOTS 8

/* Reserve un emplacement. Rend son indice, ou -1 s'il n'en reste plus.
   A appeler une fois par variable, typiquement au demarrage. */
int   dkr_tls_reserve(void);

void *dkr_tls_get(int slot);
void  dkr_tls_set(int slot, void *value);

/* Libere le bloc du fil courant. Appelee automatiquement a la fin des fils
   demarres par `dkr_thread_start` ; a appeler a la main pour un fil que cette
   couche n'a pas cree et qui se termine. */
void  dkr_tls_release_current(void);

/* --- Diagnostic ----------------------------------------------------------- *
 *
 * Appelee quand la couche constate une faute qu'elle ne peut pas rattraper —
 * aujourd'hui la reentrance sur un `dkr_mutex`. Par defaut le message part dans
 * le journal de demarrage puis le processus s'arrete : sur la machine cible,
 * continuer apres une faute de synchronisation ne produit qu'un gel plus loin,
 * sans rapport visible avec sa cause.
 *
 * Les tests l'interceptent pour verifier que la detection fonctionne.
 */
typedef void (*dkr_threading_fatal_fn)(const char *message);
void dkr_threading_set_fatal_handler(dkr_threading_fatal_fn handler);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_THREADING_H */
