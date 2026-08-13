/* E02-S02 — pont C++ entre `ultramodern` et la couche de fils de E02-S01.
 *
 * `ultramodern` emploie `std::thread`, `std::mutex` et `std::lock_guard`. Sous
 * Windows 95, les inclure suffit a rendre le binaire inchargeable : `<thread>`
 * et `<mutex>` font apparaitre six symboles que le systeme n'exporte pas, et
 * cela **sans qu'aucune de leurs fonctions ne soit appelee** (E00-S01).
 *
 * Ce fichier fournit les trois types, avec exactement la surface qu'`ultramodern`
 * utilise et pas davantage, au-dessus de l'interface C de `threading.h`.
 *
 * ## Ce qu'`ultramodern` demande reellement, releve dans son code **patche**
 *
 * Le releve porte sur l'arbre avec les quatorze patchs du depot appliques,
 * c'est-a-dire celui qu'on compile. Le faire sur l'arbre amont a d'abord conduit
 * a une conclusion fausse : voir la correction en tete de
 * `docs/WIN95-THREADING.md`.
 *
 *   thread              construction par defaut (membre de structure)
 *                       construction variadique  std::thread{f, a, b, c}
 *                       affectation par deplacement
 *                       join, detach
 *   mutex               construction par defaut, plus lock/unlock pour la
 *                       variable de condition
 *   lock_guard          les deux formes, `lock_guard lock{m}` (deduction) et
 *                       `lock_guard<mutex> lock(m)`
 *   unique_lock         construction par deduction, et le passage a `wait`
 *   condition_variable  notify_one, notify_all, wait(lock, predicat), wait_for
 *                       — introduites par le patch 0013 du depot, dans
 *                       `mesgqueue.cpp`
 *
 * Ce qui n'y est **pas**, et qui n'est donc pas fourni :
 *
 *   - `this_thread::sleep_for` / `sleep_until` — `timer.cpp` a deja une branche
 *     `#ifdef _WIN32` qui appelle `Sleep` directement, precisement parce que la
 *     STL de Microsoft s'est mal comportee sur un recul de l'horloge. La cible
 *     Windows 95 emprunte cette branche.
 *   - `unique_lock` cessible pour de bon — ni `try_lock`, ni `defer_lock`, ni
 *     transfert de propriete : aucun site d'appel n'en fait usage.
 *
 * ## Ce qui differe de la bibliotheque standard
 *
 * `std::thread` **appelle `std::terminate`** si on le detruit encore joignable.
 * Ce pont fait de meme, par le gestionnaire de faute de `threading.h`, pour ne
 * pas transformer un defaut de cycle de vie en fuite silencieuse.
 *
 * `std::thread::join` sur un fil non joignable leve `std::system_error` ; ici
 * c'est aussi une faute signalee. `ultramodern` ne le fait pas.
 *
 * La difference de reentrance de `mutex` est celle de `dkr_mutex`, decrite dans
 * `docs/WIN95-THREADING.md` : une reentrance est signalee au lieu de reussir
 * silencieusement comme le ferait une CRITICAL_SECTION nue.
 */
#ifndef DKR_WIN95_THREADING_HPP
#define DKR_WIN95_THREADING_HPP

#include <chrono>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "threading.h"

namespace dkr {
namespace win95 {

/* --- Fil ------------------------------------------------------------------ */

class thread {
public:
    thread() noexcept : handle_(nullptr) {}

    /* Le garde `enable_if` empeche ce constructeur de capturer les
       constructions par copie et par deplacement, qu'il serait sinon meilleur
       candidat a satisfaire. C'est la meme precaution que celle de la
       bibliotheque standard, et son absence produit des erreurs illisibles. */
    template <class Fn, class... Args,
              class = typename std::enable_if<
                  !std::is_same<typename std::decay<Fn>::type, thread>::value>::type>
    explicit thread(Fn &&fn, Args &&...args)
    {
        /* Les arguments sont **copies**, comme le fait `std::thread` : le fil
           cree survit a la portee qui l'a lance, et une reference y pendrait. */
        using pack_t = std::tuple<typename std::decay<Fn>::type,
                                  typename std::decay<Args>::type...>;
        pack_t *pack = new pack_t(std::forward<Fn>(fn), std::forward<Args>(args)...);

        handle_ = dkr_thread_start(&thread::entry<pack_t>, pack, 0);
        if (handle_ == nullptr) {
            delete pack;
            dkr_threading_fatal("dkr::win95::thread : creation de fil impossible");
        }
    }

    thread(thread &&other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    thread &operator=(thread &&other) noexcept
    {
        if (this != &other) {
            /* `std::thread` termine le programme si on ecrase un fil encore
               joignable. On ne fait pas mieux en silence. */
            if (handle_ != nullptr) {
                dkr_threading_fatal(
                    "dkr::win95::thread : affectation sur un fil encore joignable");
                /* Le gestionnaire par defaut ne rend pas la main. Un test qui
                   l'intercepte, si : il faut alors relacher l'ancien fil plutot
                   que d'en perdre le descripteur. Attendre ne serait pas une
                   option — on ignore combien de temps il tournera encore. */
                dkr_thread_release(handle_);
            }
            handle_       = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    thread(const thread &)            = delete;
    thread &operator=(const thread &) = delete;

    ~thread()
    {
        if (handle_ != nullptr) {
            dkr_threading_fatal(
                "dkr::win95::thread : detruit alors qu'il est encore joignable");
            dkr_thread_release(handle_);   /* idem : voir l'affectation */
        }
    }

    bool joinable() const noexcept { return handle_ != nullptr; }

    void join()
    {
        if (handle_ == nullptr) {
            dkr_threading_fatal("dkr::win95::thread : join sur un fil non joignable");
            return;
        }
        dkr_thread_join(handle_);
        handle_ = nullptr;
    }

    void detach()
    {
        if (handle_ == nullptr) {
            dkr_threading_fatal("dkr::win95::thread : detach sur un fil non joignable");
            return;
        }
        dkr_thread_release(handle_);
        handle_ = nullptr;
    }

private:
    template <class Pack>
    static void entry(void *raw)
    {
        Pack *pack = static_cast<Pack *>(raw);
        call(*pack, std::make_index_sequence<std::tuple_size<Pack>::value - 1>{});
        delete pack;
    }

    /* L'element 0 du paquet est l'appelable, les suivants ses arguments. On
       appelle directement plutot que par `std::invoke` : `<functional>` n'a
       aucune raison d'entrer ici, et aucun appel d'`ultramodern` n'est un
       pointeur sur membre. */
    template <class Pack, std::size_t... I>
    static void call(Pack &pack, std::index_sequence<I...>)
    {
        std::get<0>(pack)(std::get<I + 1>(pack)...);
    }

    dkr_thread *handle_;
};

/* --- Exclusion mutuelle --------------------------------------------------- */

class mutex {
public:
    mutex()
    {
        if (!dkr_mutex_init(&m_)) {
            dkr_threading_fatal("dkr::win95::mutex : initialisation impossible");
        }
    }

    ~mutex() { dkr_mutex_destroy(&m_); }

    mutex(const mutex &)            = delete;
    mutex &operator=(const mutex &) = delete;

    void lock()     { dkr_mutex_lock(&m_); }
    void unlock()   { dkr_mutex_unlock(&m_); }
    bool try_lock() { return dkr_mutex_try_lock(&m_) != 0; }

    /* Reserve a `condition_variable`, qui doit relacher puis reprendre ce
       verrou pendant l'attente. `std::mutex::native_handle` existe pour la
       meme raison. */
    dkr_mutex *native_handle() { return &m_; }

private:
    dkr_mutex m_;
};

/* --- Temporisation --------------------------------------------------------- *
 *
 * `std::this_thread::sleep_for` prend une duree de `<chrono>`, en-tete qui
 * fonctionne sur la cible — ce sont `<thread>` et `<mutex>` qui n'y passent pas.
 * La duree est donc acceptee telle quelle et convertie en millisecondes, la
 * seule granularite que `Sleep` de Windows 95 connaisse.
 *
 * Une duree inferieure a la milliseconde ne s'arrondit pas a zero mais a un :
 * un appelant qui demande a ceder la main brievement doit ceder la main, et
 * `Sleep(0)` ne rend pas forcement le processeur a un autre fil.
 */
namespace this_thread {

template <class Rep, class Period>
inline void sleep_for(const std::chrono::duration<Rep, Period> &d)
{
    const long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    if (ms <= 0) {
        dkr_sleep_ms(d.count() > 0 ? 1 : 0);
        return;
    }
    dkr_sleep_ms(static_cast<unsigned long>(ms));
}

} // namespace this_thread

/* --- Garde de verrou ------------------------------------------------------ */

template <class Mutex>
class lock_guard {
public:
    explicit lock_guard(Mutex &m) : m_(m) { m_.lock(); }
    ~lock_guard() { m_.unlock(); }

    lock_guard(const lock_guard &)            = delete;
    lock_guard &operator=(const lock_guard &) = delete;

private:
    Mutex &m_;
};

/* `lock_guard lock{ mutex }` sans parametre explicite : `ultramodern` emploie
   les deux formes, et la deduction est celle que C++17 donne gratuitement. */
template <class Mutex>
lock_guard(Mutex &) -> lock_guard<Mutex>;

/* --- Garde a un seul verrou ------------------------------------------------ *
 *
 * `std::scoped_lock` est variadique, et c'est la forme dominante dans les
 * sources du jeu : 95 emplois, tous sur **un seul** verrou.
 *
 * Elle n'est donc fournie que pour un verrou, et le choix merite d'etre dit,
 * parce que l'alternative parait plus complete et serait pire. Une version
 * variadique naive verrouillerait dans l'ordre des arguments — ce que
 * `std::scoped_lock` ne fait justement pas : elle emploie l'algorithme de
 * `std::lock`, qui evite l'interblocage par acquisitions et abandons repetes.
 * Reproduire la signature sans reproduire cet algorithme donnerait du code qui
 * compile, marche a l'essai, et interbloque un jour sous cadence.
 *
 * Avec un seul parametre, deux verrous ne compilent pas. L'echec est visible,
 * a l'endroit fautif, et le jour ou le code du jeu en aura besoin il faudra
 * ecrire l'algorithme — pas le contourner sans le savoir.
 */
template <class Mutex>
class scoped_lock {
public:
    explicit scoped_lock(Mutex &m) : m_(m) { m_.lock(); }
    ~scoped_lock() { m_.unlock(); }

    scoped_lock(const scoped_lock &)            = delete;
    scoped_lock &operator=(const scoped_lock &) = delete;

private:
    Mutex &m_;
};

template <class Mutex>
scoped_lock(Mutex &) -> scoped_lock<Mutex>;

/* --- Verrou cessible ------------------------------------------------------ *
 *
 * `std::unique_lock` a une large surface ; `mesgqueue.cpp` n'en emploie que la
 * construction — trois fois, toujours par deduction — et le passage a
 * `condition_variable::wait`. C'est donc tout ce qui est fourni, plus
 * `lock`/`unlock`, dont la variable de condition a besoin.
 */
template <class Mutex>
class unique_lock {
public:
    explicit unique_lock(Mutex &m) : m_(&m), owns_(true) { m_->lock(); }

    ~unique_lock()
    {
        if (owns_) {
            m_->unlock();
        }
    }

    unique_lock(const unique_lock &)            = delete;
    unique_lock &operator=(const unique_lock &) = delete;

    void lock()                { m_->lock();   owns_ = true; }
    void unlock()              { m_->unlock(); owns_ = false; }
    bool owns_lock() const     { return owns_; }

    /* Reserve a `condition_variable`, qui doit atteindre le verrou nu pour le
       relacher pendant l'attente. */
    Mutex *release_to_condvar() const { return m_; }

private:
    Mutex *m_;
    bool   owns_;
};

template <class Mutex>
unique_lock(Mutex &) -> unique_lock<Mutex>;

/* --- Variable de condition ------------------------------------------------ *
 *
 * Le raisonnement de correction — pourquoi aucun reveil ne se perd, et ce qui
 * n'est deliberement pas garanti — est dans `threading.h`, au-dessus de
 * `dkr_condvar`. Ici il n'y a que l'habillage.
 */
enum class cv_status { no_timeout, timeout };

class condition_variable {
public:
    condition_variable()
    {
        if (!dkr_condvar_init(&cv_)) {
            dkr_threading_fatal(
                "dkr::win95::condition_variable : initialisation impossible");
        }
    }

    ~condition_variable() { dkr_condvar_destroy(&cv_); }

    condition_variable(const condition_variable &)            = delete;
    condition_variable &operator=(const condition_variable &) = delete;

    void notify_one() { dkr_condvar_notify_one(&cv_); }
    void notify_all() { dkr_condvar_notify_all(&cv_); }

    template <class Lock>
    void wait(Lock &lock)
    {
        dkr_condvar_wait(&cv_, lock.release_to_condvar()->native_handle());
    }

    /* La forme a predicat, celle qu'emploie `ExternalMessageQueue::wait_dequeue`.
       La boucle n'est pas une precaution : un reveil peut etre derobe par un
       autre attendeur, ici comme avec la bibliotheque standard. */
    template <class Lock, class Predicate>
    void wait(Lock &lock, Predicate stop_waiting)
    {
        while (!stop_waiting()) {
            wait(lock);
        }
    }

    template <class Lock, class Rep, class Period>
    cv_status wait_for(Lock &lock, const std::chrono::duration<Rep, Period> &d)
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
        if (ms < 0) {
            ms = 0;
        }
        int woken = dkr_condvar_wait_timeout(
            &cv_, lock.release_to_condvar()->native_handle(),
            static_cast<unsigned long>(ms));
        return woken ? cv_status::no_timeout : cv_status::timeout;
    }

    /* La forme a predicat, employee par `ExternalMessageQueue::wait_dequeue_timed`.
       Elle rend la valeur du predicat, comme la bibliotheque standard, et non
       « ai-je ete reveille » : un reveil derobe ne doit pas se traduire par un
       faux positif chez l'appelant.

       L'echeance est **globale** et non par tour. La reprendre a chaque tour
       serait le defaut classique de cette fonction : sous des reveils repetes,
       l'attente ne finirait jamais. */
    template <class Lock, class Rep, class Period, class Predicate>
    bool wait_for(Lock &lock, const std::chrono::duration<Rep, Period> &d,
                  Predicate stop_waiting)
    {
        const auto deadline = std::chrono::steady_clock::now() + d;
        while (!stop_waiting()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return stop_waiting();
            }
            wait_for(lock, deadline - now);
        }
        return true;
    }

private:
    dkr_condvar cv_;
};

} // namespace win95
} // namespace dkr

#endif /* DKR_WIN95_THREADING_HPP */
