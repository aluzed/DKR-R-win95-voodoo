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

/* --- Variable de condition ------------------------------------------------ *
 *
 * Windows 95 n'en a pas : les siennes datent de Vista. Celle-ci est batie sur
 * le semaphore ci-dessus et un compteur d'attendeurs protege par un verrou.
 *
 * C'est le morceau que le ticket E02-S01 redoutait — « un exercice ou l'on perd
 * des reveils ». Il s'est avere necessaire non pas a cause d'`ultramodern`
 * amont, qui n'en emploie aucune, mais a cause du patch 0013 du depot, qui en
 * introduit deux dans `mesgqueue.cpp`.
 *
 * ## Pourquoi aucun reveil ne se perd
 *
 * La fenetre dangereuse d'une variable de condition est celle-ci : l'attendeur
 * relache le verrou de l'appelant, puis se met en attente. Un signal emis
 * *entre les deux* doit lui parvenir quand meme.
 *
 * Ici il lui parvient, parce que le primitif d'attente est un **semaphore de
 * comptage** : `notify` depose un jeton, et le jeton attend l'attendeur.
 *
 * Et surtout : le compteur d'attendeurs est incremente **avant** que le verrou
 * de l'appelant ne soit relache. Cet ordre n'est pas une precaution, c'est la
 * demonstration. Un signaleur ne peut signaler qu'apres avoir modifie l'etat
 * que l'attendeur teste, et il ne peut le modifier qu'en tenant ce meme verrou.
 * Il ne peut donc pas prendre le verrou tant que nous ne l'avons pas relache —
 * or a cet instant nous sommes deja inscrits. Il n'existe aucun entrelacement
 * ou il nous manque.
 *
 * **Cette propriete tient par l'argument, non par le test.** L'ordre inverse a
 * ete essaye : la suite passe quand meme, 20 000 relais compris. La raison est
 * instructive — le chemin du signaleur jusqu'a `notify` (prendre le verrou,
 * modifier l'etat, le relacher) est plus long que celui de l'attendeur jusqu'a
 * son inscription, de sorte qu'il perd presque toujours la course. Presque.
 * C'est exactement la forme de defaut que le ticket decrit : rare, non
 * deterministe, et qui se manifeste en gel aleatoire chez le joueur. On ne le
 * traite donc pas par le test mais par la construction.
 *
 * ## Ce qui n'est pas garanti, et qui ne l'est pas non plus ailleurs
 *
 * Un reveil peut etre **derobe** : si deux fils attendent et qu'un troisieme
 * signale, rien ne dit lequel des deux repart. `std::condition_variable` ne le
 * dit pas davantage, et c'est pourquoi tout appelant correct enveloppe son
 * attente dans une boucle sur un predicat. Les deux sites d'appel du depot le
 * font — `wait(lock, predicat)` et `while (!complete && !exited)`.
 *
 * `notify` emis alors que personne n'attend est perdu, comme il se doit.
 */
typedef struct {
    dkr_mutex  guard;        /* protege `waiters` */
    dkr_sem    sem;          /* le primitif d'attente proprement dit */
    long       waiters;
    int        initialised;
} dkr_condvar;

int  dkr_condvar_init(dkr_condvar *cv);
void dkr_condvar_destroy(dkr_condvar *cv);

/* Reveille au plus un attendeur, au plus tous. Sans effet s'il n'y en a aucun. */
void dkr_condvar_notify_one(dkr_condvar *cv);
void dkr_condvar_notify_all(dkr_condvar *cv);

/* Relache `external`, attend, puis le reprend avant de rendre la main — y
   compris en cas d'expiration, comme `std::condition_variable`.
   `wait` rend 1 ; `wait_timeout` rend 1 s'il a ete reveille, 0 s'il a expire. */
int  dkr_condvar_wait(dkr_condvar *cv, dkr_mutex *external);
int  dkr_condvar_wait_timeout(dkr_condvar *cv, dkr_mutex *external,
                              unsigned long ms);

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

/* Signale une faute par ce meme canal. Publique parce que le pont C++ de
   E02-S02 (`threading.hpp`) en a besoin : un `thread` detruit encore joignable
   est la meme classe de faute qu'une reentrance, et doit se signaler et se
   tester de la meme facon.

   Ne rend la main que si un gestionnaire l'a interceptee — le comportement par
   defaut est d'arreter le processus. Les appelants doivent donc rester corrects
   dans les deux cas. */
void dkr_threading_fatal(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* DKR_WIN95_THREADING_H */
