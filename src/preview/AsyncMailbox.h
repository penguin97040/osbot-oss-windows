// One outstanding asynchronous read. Callbacks own this state, never the UI or
// preview object, so late callbacks remain safe after the worker has stopped.
#pragma once

#include <condition_variable>
#include <mutex>
#include <utility>

template<class Result> class AsyncMailbox {
public:
    bool BeginRead() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_ || pending_ || ready_) return false;
        pending_ = true;
        return true;
    }
    void Deliver(Result result) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_ || !pending_) return;
        result_ = std::move(result);
        pending_ = false;
        ready_ = true;
        cv_.notify_all();
    }
    bool Wait(Result& result) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return cancelled_ || ready_; });
        if (cancelled_) return false;
        result = std::move(result_);
        result_ = {};
        ready_ = false;
        return true;
    }
    void Cancel() {
        [[maybe_unused]] Result discarded{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
            discarded = std::move(result_);
            result_ = {};
            cv_.notify_all();
        }
        // Release callback resources outside the lock, allowing COM re-entrancy.
    }
    template<class Publish> void IfActive(Publish publish) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cancelled_) publish();
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_ = false, ready_ = false, cancelled_ = false;
    Result result_{};
};
