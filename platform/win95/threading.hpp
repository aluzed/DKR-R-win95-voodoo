/* E02-S02 - C++ bridge between `ultramodern` and E02-S01's threading layer.
 *
 * `ultramodern` uses `std::thread`, `std::mutex` and `std::lock_guard`. Under
 * Windows 95, including them is enough to make the binary unloadable:
 * `<thread>` and `<mutex>` bring in six symbols the system does not export, and
 * that **without any of their functions being called** (E00-S01).
 *
 * This file supplies the three types, with exactly the surface `ultramodern`
 * uses and no more, on top of `threading.h`'s C interface.
 *
 * ## What `ultramodern` actually asks for, surveyed in its **patched** code
 *
 * The survey covers the tree with the repository's fourteen patches applied,
 * that is, the one we compile. Doing it on the upstream tree first led to a
 * wrong conclusion: see the correction at the top of
 * `docs/WIN95-THREADING.md`.
 *
 *   thread              default construction (struct member)
 *                       variadic construction  std::thread{f, a, b, c}
 *                       move assignment
 *                       join, detach
 *   mutex               default construction, plus lock/unlock for the
 *                       condition variable
 *   lock_guard          both forms, `lock_guard lock{m}` (deduction) and
 *                       `lock_guard<mutex> lock(m)`
 *   unique_lock         construction by deduction, and passing to `wait`
 *   condition_variable  notify_one, notify_all, wait(lock, predicate), wait_for
 *                       - introduced by the repository's patch 0013, in
 *                       `mesgqueue.cpp`
 *
 * What is **not** there, and is therefore not supplied:
 *
 *   - `this_thread::sleep_for` / `sleep_until` - `timer.cpp` already has an
 *     `#ifdef _WIN32` branch that calls `Sleep` directly, precisely because
 *     Microsoft's STL misbehaved on a clock backstep. The Windows 95 target
 *     takes that branch.
 *   - a fully transferable `unique_lock` - no `try_lock`, no `defer_lock`, no
 *     ownership transfer: no call site uses them.
 *
 * ## What differs from the standard library
 *
 * `std::thread` **calls `std::terminate`** if it is destroyed while still
 * joinable. This bridge does the same, through `threading.h`'s fault handler, so
 * as not to turn a lifetime defect into a silent leak.
 *
 * `std::thread::join` on a non-joinable thread throws `std::system_error`; here
 * it is also a reported fault. `ultramodern` does not do it.
 *
 * `mutex`'s reentrancy difference is `dkr_mutex`'s, described in
 * `docs/WIN95-THREADING.md`: a reentrancy is reported instead of succeeding
 * silently as a bare CRITICAL_SECTION would.
 */
#ifndef DKR_WIN95_THREADING_HPP
#define DKR_WIN95_THREADING_HPP

#include <chrono>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "threading.h"

namespace dkr {
namespace win95 {

/* --- Thread --------------------------------------------------------------- */

class thread {
public:
    thread() noexcept : handle_(nullptr) {}

    /* The `enable_if` guard stops this constructor from capturing the copy and
       move constructions, which it would otherwise be a better candidate for.
       It is the same precaution the standard library takes, and its absence
       produces unreadable errors. */
    template <class Fn, class... Args,
              class = typename std::enable_if<
                  !std::is_same<typename std::decay<Fn>::type, thread>::value>::type>
    explicit thread(Fn &&fn, Args &&...args)
    {
        /* The arguments are **copied**, as `std::thread` does: the created
           thread outlives the scope that launched it, and a reference would
           dangle. */
        using pack_t = std::tuple<typename std::decay<Fn>::type,
                                  typename std::decay<Args>::type...>;
        pack_t *pack = new pack_t(std::forward<Fn>(fn), std::forward<Args>(args)...);

        handle_ = dkr_thread_start(&thread::entry<pack_t>, pack, 0);
        if (handle_ == nullptr) {
            delete pack;
            dkr_threading_fatal("dkr::win95::thread: cannot create thread");
        }
    }

    thread(thread &&other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    thread &operator=(thread &&other) noexcept
    {
        if (this != &other) {
            /* `std::thread` terminates the program if a still-joinable thread
               is overwritten. We do no better in silence. */
            if (handle_ != nullptr) {
                dkr_threading_fatal(
                    "dkr::win95::thread: assignment over a still-joinable thread");
                /* The default handler does not return. A test that intercepts it
                   does: the old thread must then be released rather than have
                   its handle lost. Waiting would not be an option - we do not
                   know how much longer it will run. */
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
                "dkr::win95::thread: destroyed while still joinable");
            dkr_thread_release(handle_);   /* likewise: see the assignment */
        }
    }

    bool joinable() const noexcept { return handle_ != nullptr; }

    void join()
    {
        if (handle_ == nullptr) {
            dkr_threading_fatal("dkr::win95::thread: join on a non-joinable thread");
            return;
        }
        dkr_thread_join(handle_);
        handle_ = nullptr;
    }

    void detach()
    {
        if (handle_ == nullptr) {
            dkr_threading_fatal("dkr::win95::thread: detach on a non-joinable thread");
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

    /* Element 0 of the pack is the callable, the rest are its arguments.
     *
     * This code called directly, `std::get<0>(pack)(...)`, resting on a remark
     * that was accurate but too narrow: none of `ultramodern`'s calls was a
     * pointer to member. `librecomp` has one - `mods.cpp` starts a thread on
     * `&ModContext::dirty_mod_configuration_thread_process` - and the direct
     * form does not compile for it.
     *
     * `std::invoke` is what `std::thread` uses, and it is the contract we
     * reproduce here. `<functional>` is pure library: it asks nothing of the
     * system and adds no import. The original assumption saved a header at the
     * price of a contract divergence - the wrong side of that bargain. */
    template <class Pack, std::size_t... I>
    static void call(Pack &pack, std::index_sequence<I...>)
    {
        std::invoke(std::get<0>(pack), std::get<I + 1>(pack)...);
    }

    dkr_thread *handle_;
};

/* --- Mutual exclusion ----------------------------------------------------- */

class mutex {
public:
    mutex()
    {
        if (!dkr_mutex_init(&m_)) {
            dkr_threading_fatal("dkr::win95::mutex: cannot initialise");
        }
    }

    ~mutex() { dkr_mutex_destroy(&m_); }

    mutex(const mutex &)            = delete;
    mutex &operator=(const mutex &) = delete;

    void lock()     { dkr_mutex_lock(&m_); }
    void unlock()   { dkr_mutex_unlock(&m_); }
    bool try_lock() { return dkr_mutex_try_lock(&m_) != 0; }

    /* Reserved for `condition_variable`, which must release and then retake this
       lock during the wait. `std::mutex::native_handle` exists for the same
       reason. */
    dkr_mutex *native_handle() { return &m_; }

private:
    dkr_mutex m_;
};

/* --- Delays ---------------------------------------------------------------- *
 *
 * `std::this_thread::sleep_for` takes a `<chrono>` duration, a header that works
 * on the target - it is `<thread>` and `<mutex>` that do not. The duration is
 * therefore accepted as it is and converted to milliseconds, the only
 * granularity Windows 95's `Sleep` knows.
 *
 * A duration shorter than a millisecond does not round to zero but to one: a
 * caller asking to yield briefly must yield, and `Sleep(0)` does not necessarily
 * hand the processor to another thread.
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

/* `sleep_until` is expressed in terms of `sleep_for` rather than an absolute
   clock: Windows 95 has no wait-until-a-date, and the difference between the two
   reduces here to a subtraction. A date already past returns immediately, like
   the standard library. */
template <class Clock, class Duration>
inline void sleep_until(const std::chrono::time_point<Clock, Duration> &when)
{
    const auto now = Clock::now();
    if (when > now) {
        sleep_for(when - now);
    }
}

/* `yield` gives up the rest of the quantum. `Sleep(0)` only hands it to a
   runnable thread of the same priority, and to nothing at all otherwise - that
   is Windows 95's behaviour, and `dkr_yield` makes of it what it can. */
inline void yield()
{
    dkr_yield();
}

} // namespace this_thread

/* --- Lock guard ----------------------------------------------------------- */

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

/* `lock_guard lock{ mutex }` with no explicit parameter: `ultramodern` uses both
   forms, and the deduction is the one C++17 gives for free. */
template <class Mutex>
lock_guard(Mutex &) -> lock_guard<Mutex>;

/* --- Single-lock guard ----------------------------------------------------- *
 *
 * `std::scoped_lock` is variadic, and it is the dominant form in the game's
 * sources: 95 uses, all on **one** lock.
 *
 * It is therefore only supplied for one lock, and the choice is worth stating,
 * because the alternative looks more complete and would be worse. A naive
 * variadic version would lock in argument order - which is precisely what
 * `std::scoped_lock` does not do: it uses `std::lock`'s algorithm, which avoids
 * deadlock through repeated acquisition and back-off. Reproducing the signature
 * without reproducing that algorithm would give code that compiles, works in
 * trials, and deadlocks one day under load.
 *
 * With a single parameter, two locks do not compile. The failure is visible, at
 * the offending place, and the day the game's code needs it the algorithm will
 * have to be written - not worked around unknowingly.
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

/* --- Transferable lock ----------------------------------------------------- *
 *
 * `std::unique_lock` has a wide surface; `mesgqueue.cpp` only uses its
 * construction - three times, always by deduction - and passing it to
 * `condition_variable::wait`. That is therefore all that is supplied, plus
 * `lock`/`unlock`, which the condition variable needs.
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

    /* Reserved for `condition_variable`, which must reach the bare lock in order
       to release it during the wait. */
    Mutex *release_to_condvar() const { return m_; }

private:
    Mutex *m_;
    bool   owns_;
};

template <class Mutex>
unique_lock(Mutex &) -> unique_lock<Mutex>;

/* --- Condition variable --------------------------------------------------- *
 *
 * The correctness argument - why no wake-up is lost, and what is deliberately
 * not guaranteed - is in `threading.h`, above `dkr_condvar`. Here there is only
 * the wrapper.
 */
enum class cv_status { no_timeout, timeout };

class condition_variable {
public:
    condition_variable()
    {
        if (!dkr_condvar_init(&cv_)) {
            dkr_threading_fatal(
                "dkr::win95::condition_variable: cannot initialise");
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

    /* The predicate form, the one `ExternalMessageQueue::wait_dequeue` uses.
       The loop is not a precaution: a wake-up can be stolen by another waiter,
       here as with the standard library. */
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

    /* The predicate form, used by `ExternalMessageQueue::wait_dequeue_timed`.
       It returns the predicate's value, like the standard library, and not
       "was I woken": a stolen wake-up must not turn into a false positive at the
       caller.

       The deadline is **global** and not per turn. Restarting it on every turn
       would be this function's classic defect: under repeated wake-ups, the wait
       would never end. */
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
