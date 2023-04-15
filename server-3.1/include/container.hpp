#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace woody_server {
template <typename T, typename Container> struct threadsafe_container_t {
private:
  std::mutex m_mutex{};
  Container m_container{};
  std::size_t m_total{};
  std::condition_variable m_cv{};

public:
  explicit threadsafe_container_t(Container &&container)
      : m_container{std::move(container)}, m_total{m_container.size()} {}

  threadsafe_container_t() = default;

  threadsafe_container_t(threadsafe_container_t &&vec)
      : m_mutex{std::move(vec.m_mutex)}, m_container{std::move(
                                             vec.m_container)},
        m_total{vec.m_total}, m_cv{std::move(vec.m_cv)} {}

  threadsafe_container_t &operator=(threadsafe_container_t &&) = delete;

  threadsafe_container_t(threadsafe_container_t const &) = delete;

  threadsafe_container_t &operator=(threadsafe_container_t const &) = delete;

  T get() {
    std::unique_lock<std::mutex> u_lock{m_mutex};
    m_cv.wait(u_lock, [this] { return !m_container.empty(); });
    T value{std::move(m_container.front())};
    m_container.pop_front();
    m_total = m_container.size();
    return value;
  }

  template <typename U, typename Func>
  std::vector<T> removeValue(U &&keys, Func &&function) {
    if (m_container.empty())
      return {};
    std::unique_lock<std::mutex> u_lock{m_mutex};
    std::vector<T> result{};
    for (auto &value : m_container) {
      if (function(value, keys))
        result.emplace_back(std::move(value));
    }
    return result;
  }

  template <typename U> void append(U &&data) {
    std::lock_guard<std::mutex> lock_{m_mutex};
    m_container.push_back(std::forward<U>(data));
    m_total = m_container.size();
    m_cv.notify_one();
  }

  bool isEmpty() {
    std::lock_guard<std::mutex> lock_{m_mutex};
    return m_container.empty();
  }

  // std::size_t getTotal() const { return m_total; }

  std::size_t size() {
    std::lock_guard<std::mutex> lock_{m_mutex};
    return m_container.size();
  }
};

template <typename T>
using threadsafe_list_t = threadsafe_container_t<T, std::deque<T>>;
} // namespace woody_server
