/**
 * @file Signal.h
 * @brief Minimal thread-safe, typed signal/slot primitive (replacement for Poco::BasicEvent).
 *
 * Guarantees:
 *  - connect()/disconnect() may be called at any time, from any thread, even
 *    from inside a handler (copy-on-write slot list, no lock held while a
 *    handler runs).
 *  - Connection::disconnect() (and the Connection destructor) wait until
 *    handlers of that slot running on *other* threads have returned, so an
 *    observer can safely be destroyed right after disconnecting. A handler
 *    that disconnects itself does not wait for itself.
 *  - Handlers can run concurrently on different threads and therefore must be
 *    thread-safe. Exceptions escaping a handler are swallowed.
 *  - A Connection stays valid (and becomes a no-op) if the Signal is destroyed
 *    before it.
 */
#pragma once

#include "Export.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace TK4CPP::TP {

/**
 * @brief RAII handle of a signal subscription.
 *
 * Move-only. The subscription is removed when the handle is destroyed or when
 * disconnect() is called. Use release() to keep the subscription alive for the
 * lifetime of the signal.
 */
class [[nodiscard]] Connection {
public:
    Connection() noexcept = default;
    Connection(Connection&& o) noexcept : _disconnect(std::exchange(o._disconnect, nullptr)) {}
    Connection& operator=(Connection&& o) noexcept {
        if (this != &o) {
            disconnect();
            _disconnect = std::exchange(o._disconnect, nullptr);
        }
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { disconnect(); }

    /** @brief Removes the subscription (idempotent). */
    void disconnect() noexcept {
        if (auto fn = std::exchange(_disconnect, nullptr)) fn();
    }

    /** @brief Detaches the handle: the subscription lives as long as the signal. */
    void release() noexcept { _disconnect = nullptr; }

    /** @return true while the handle owns a subscription. */
    [[nodiscard]] bool connected() const noexcept { return static_cast<bool>(_disconnect); }

private:
    template<class E> friend class Signal;
    explicit Connection(std::function<void()> fn) : _disconnect(std::move(fn)) {}
    std::function<void()> _disconnect;
};

namespace detail {

/**
 * Slots currently being invoked by the calling thread (used to allow self-disconnect).
 * Defined in the library (not inline) so that every shared object shares one thread_local.
 */
TK4CPP_TP_API std::vector<const void*>& activeSlotsOnThisThread();

} // namespace detail

/**
 * @brief Signal carrying events of type @p E.
 * @tparam E Event payload, passed to handlers as `const E&`.
 */
template<class E>
class Signal {
public:
    using Handler = std::function<void(const E&)>;

    Signal() : _state(std::make_shared<State>()) {}
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    /**
     * @brief Registers a handler.
     * @return Connection that removes the handler when destroyed.
     */
    [[nodiscard]] Connection connect(Handler handler) {
        auto slot = std::make_shared<Slot>();
        slot->fn = std::move(handler);
        {
            std::lock_guard lk(_state->mutex);
            auto next = std::make_shared<SlotList>(*_state->slots);
            next->push_back(slot);
            _state->count.store(next->size(), std::memory_order_release);
            _state->slots = std::move(next);
        }
        return Connection([weakState = std::weak_ptr<State>(_state), slot]() {
            if (auto st = weakState.lock()) removeSlot(*st, slot);
            waitForInflight(*slot);
        });
    }

    /** @return true if at least one handler is registered (cheap, lock-free). */
    [[nodiscard]] bool hasListeners() const noexcept {
        return _state->count.load(std::memory_order_acquire) != 0;
    }

    /** @brief Invokes every registered handler with @p event, in registration order. */
    void emit(const E& event) const noexcept {
        std::shared_ptr<const SlotList> snapshot;
        {
            std::lock_guard lk(_state->mutex);
            snapshot = _state->slots;
        }
        for (const auto& slot : *snapshot) {
            {
                std::lock_guard lk(slot->mutex);
                if (!slot->alive) continue;
                ++slot->inflight;
            }
            auto& active = detail::activeSlotsOnThisThread();
            active.push_back(slot.get());
            try { slot->fn(event); } catch (...) { /* handlers must not break the pool */ }
            active.pop_back();
            {
                std::lock_guard lk(slot->mutex);
                if (--slot->inflight == 0) slot->cv.notify_all();
            }
        }
    }

private:
    struct Slot {
        std::mutex              mutex;
        std::condition_variable cv;
        std::size_t             inflight = 0;
        bool                    alive    = true;
        Handler                 fn;
    };
    using SlotList = std::vector<std::shared_ptr<Slot>>;
    struct State {
        std::mutex                         mutex;
        std::shared_ptr<const SlotList>    slots = std::make_shared<const SlotList>();
        std::atomic<std::size_t>           count{ 0 };
    };

    static void removeSlot(State& st, const std::shared_ptr<Slot>& slot) {
        std::lock_guard lk(st.mutex);
        auto next = std::make_shared<SlotList>(*st.slots);
        next->erase(std::remove(next->begin(), next->end(), slot), next->end());
        st.count.store(next->size(), std::memory_order_release);
        st.slots = std::move(next);
    }

    static void waitForInflight(Slot& slot) {
        const auto& active = detail::activeSlotsOnThisThread();
        const std::size_t self = static_cast<std::size_t>(std::count(active.begin(), active.end(),
                                                                      static_cast<const void*>(&slot)));
        std::unique_lock lk(slot.mutex);
        slot.alive = false;
        slot.cv.wait(lk, [&] { return slot.inflight <= self; });
    }

    std::shared_ptr<State> _state;
};

} // namespace TK4CPP::TP
