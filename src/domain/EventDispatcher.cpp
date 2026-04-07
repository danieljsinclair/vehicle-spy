#include "vehicle-sim/domain/EventDispatcher.h"

#include <mutex>
#include <vector>
#include <atomic>
#include <algorithm>

namespace vehicle_sim::domain {

/**
 * Pimpl implementation for EventDispatcher
 *
 * Provides encapsulation and stable ABI
 */
class EventDispatcher::Impl {
public:
    Impl() : nextToken_(1) {}

    /**
     * Register a new consumer
     */
    unsigned int registerConsumer(EventDispatcher::SignalCallback callback) {
        std::lock_guard<std::mutex> lock(consumersMutex_);

        unsigned int token = nextToken_++;
        consumers_.push_back({std::move(callback), token, true});

        return token;
    }

    /**
     * Unregister a consumer by token
     */
    void unregisterConsumer(unsigned int token) {
        std::lock_guard<std::mutex> lock(consumersMutex_);

        auto it = std::find_if(consumers_.begin(), consumers_.end(),
            [token](const Consumer& c) { return c.token == token && c.active; });

        if (it != consumers_.end()) {
            it->active = false;
        }
    }

    /**
     * Dispatch signal to all active consumers
     */
    void dispatch(const VehicleSignal& signal) {
        std::vector<SignalCallback> activeCallbacks;

        // Copy active callbacks to minimize lock time during dispatch
        {
            std::lock_guard<std::mutex> lock(consumersMutex_);
            for (const auto& consumer : consumers_) {
                if (consumer.active) {
                    activeCallbacks.push_back(consumer.callback);
                }
            }
        }

        // Dispatch to all consumers (outside lock for concurrency)
        for (const auto& callback : activeCallbacks) {
            if (callback) {
                callback(signal);
            }
        }
    }

    /**
     * Clear all consumers
     */
    void clear() {
        std::lock_guard<std::mutex> lock(consumersMutex_);
        consumers_.clear();
    }

private:
    struct Consumer {
        SignalCallback callback;
        unsigned int token;
        bool active;
    };

    std::vector<Consumer> consumers_;
    std::mutex consumersMutex_;
    std::atomic<unsigned int> nextToken_;
};

// EventDispatcher implementation

EventDispatcher::EventDispatcher()
    : pImpl(std::make_unique<Impl>()) {
}

EventDispatcher::~EventDispatcher() = default;

unsigned int EventDispatcher::registerConsumer(SignalCallback callback) {
    return pImpl->registerConsumer(std::move(callback));
}

void EventDispatcher::unregisterConsumer(unsigned int token) {
    pImpl->unregisterConsumer(token);
}

void EventDispatcher::dispatch(const VehicleSignal& signal) {
    pImpl->dispatch(signal);
}

void EventDispatcher::clear() {
    pImpl->clear();
}

} // namespace vehicle_sim::domain
