#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace echo {

// 有界阻塞队列: 管线各级之间的通道。
// - 满时 Push 阻塞, 空时 Pop 阻塞
// - 两端都支持 stop_token 取消(用 condition_variable_any 的 C++20 重载)
template <class T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  // 队列满时阻塞等待; stop 请求到达时放弃并返回 false
  bool Push(T value, std::stop_token stop = {}) {
    std::unique_lock lock(mutex_);
    const auto has_room = [this] { return items_.size() < capacity_; };
    if (stop.stop_possible()) {
      if (!not_full_.wait(lock, stop, has_room)) return false;
    } else {
      not_full_.wait(lock, has_room);
    }
    items_.push_back(std::move(value));
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  // 非阻塞入队: 满则立即返回 false(实时音频等宁可丢帧也不能阻塞的场景)
  bool TryPush(T value) {
    {
      std::scoped_lock lock(mutex_);
      if (items_.size() >= capacity_) return false;
      items_.push_back(std::move(value));
    }
    not_empty_.notify_one();
    return true;
  }

  // 队列空时阻塞等待; stop 请求到达时返回 nullopt
  std::optional<T> Pop(std::stop_token stop = {}) {
    std::unique_lock lock(mutex_);
    const auto has_item = [this] { return !items_.empty(); };
    if (stop.stop_possible()) {
      if (!not_empty_.wait(lock, stop, has_item)) return std::nullopt;
    } else {
      not_empty_.wait(lock, has_item);
    }
    T value = std::move(items_.front());
    items_.pop_front();
    lock.unlock();
    not_full_.notify_one();
    return value;
  }

  [[nodiscard]] size_t Size() const {
    std::scoped_lock lock(mutex_);
    return items_.size();
  }

  // 丢弃所有排队元素(打断场景); 正在被消费的元素不受影响
  void Clear() {
    {
      std::scoped_lock lock(mutex_);
      items_.clear();
    }
    not_full_.notify_all();
  }

 private:
  const size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable_any not_empty_;
  std::condition_variable_any not_full_;
  std::deque<T> items_;
};

}  // namespace echo
