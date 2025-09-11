#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class EventQueue {
public:
    void Enqueue(const T& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(event);
        cond_var_.notify_one();
    }

    std::optional<T> Dequeue() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this]() { return !queue_.empty(); });
        T event = queue_.front();
        queue_.pop();
        return event;
    }

    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
};

#endif // EVENT_QUEUE_H