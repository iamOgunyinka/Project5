#pragma once

#include <array>
#include <boost/utility/string_view.hpp>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

namespace wudi_server {

namespace utilities {
struct request_handler {
  static std::array<char const *, 14> const user_agents;
};

template <typename T, typename Container = std::deque<T>, bool use_cv = false>
struct threadsafe_container {
private:
  std::mutex mutex_{};
  Container container_{};
  std::size_t total_{};

public:
  threadsafe_container(Container &&container)
      : container_{std::move(container)}, total_{container_.size()} {}
  threadsafe_container() = default;
  threadsafe_container(threadsafe_container &&vec)
      : mutex_{std::move(vec.mutex_)},
        container_{std::move(vec.container_)}, total_{vec.total_} {}
  threadsafe_container &operator=(threadsafe_container &&) = delete;
  threadsafe_container(threadsafe_container const &) = delete;
  threadsafe_container &operator=(threadsafe_container const &) = delete;

  T get() {
    std::lock_guard<std::mutex> lock{mutex_};
    if (container_.empty())
      throw std::runtime_error{};
    T value = container_.front();
    container_.pop_front();
    --total_;
    return value;
  }

  void clear() {
    std::lock_guard<std::mutex> lock{mutex_};
    container_.clear();
  }

  Container container() const { return container_; }

  template <typename U> void push_back(U &&data) {
    std::lock_guard<std::mutex> lock_{mutex_};
    container_.push_back(std::forward<U>(data));
    total_ = container_.size();
  }
  bool empty() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.empty();
  }
  std::size_t get_total() const { return total_; }
  std::size_t size() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.size();
  }
};

template <typename T, typename Container>
struct threadsafe_container<T, Container, true> {
private:
  std::mutex mutex_{};
  Container container_{};
  std::size_t total_{};
  std::condition_variable cv_{};

public:
  threadsafe_container(Container &&container)
      : container_{std::move(container)}, total_{container_.size()} {}
  threadsafe_container() = default;

  threadsafe_container(threadsafe_container &&vec)
      : mutex_{std::move(vec.mutex_)}, container_{std::move(vec.container_)},
        total_{vec.total_}, cv_{std::move(vec.cv_)} {}
  threadsafe_container &operator=(threadsafe_container &&) = delete;
  threadsafe_container(threadsafe_container const &) = delete;
  threadsafe_container &operator=(threadsafe_container const &) = delete;

  T get() {
    std::unique_lock<std::mutex> u_lock{mutex_};
    cv_.wait(u_lock, [this] { return !container_.empty(); });
    T value{std::move(container_.front())};
    container_.pop_front();
    total_ = container_.size();
    return value;
  }

  template <typename U, typename Func>
  std::vector<T> remove_value(U &&keys, Func &&function) {
    if (container_.empty())
      return {};
    std::unique_lock<std::mutex> u_lock{mutex_};
    std::vector<T> result{};
    for (auto &value : container_) {
      if (function(value, keys))
        result.emplace_back(std::move(value));
    }
    return result;
  }

  template <typename U> void push_back(U &&data) {
    std::lock_guard<std::mutex> lock_{mutex_};
    container_.push_back(std::forward<U>(data));
    total_ = container_.size();
    cv_.notify_one();
  }

  bool empty() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.empty();
  }

  std::size_t get_total() const { return total_; }

  std::size_t size() {
    std::lock_guard<std::mutex> lock_{mutex_};
    return container_.size();
  }
};

template <std::size_t N>
bool status_in_codes(std::size_t const code,
                     std::array<std::size_t, N> const &codes) {
  for (auto const &stat_code : codes)
    if (code == stat_code)
      return true;
  return false;
}

template <typename Container, typename... IterList>
bool any_of(Container const &container, IterList &&... iter_list) {
  return (... || (std::cend(container) == iter_list));
}

template <typename T> using filter = bool (*)(std::string_view const, T &);

template <typename T, typename Func>
void get_file_content(std::string const &filename, filter<T> filter,
                      Func post_op) {
  std::ifstream in_file{filename};
  if (!in_file)
    return;
  std::string line{};
  T output{};
  while (std::getline(in_file, line)) {
    if (filter(line, output))
      post_op(output);
  }
}

template <typename T>
using threadsafe_cv_container = threadsafe_container<T, std::deque<T>, true>;

std::string md5(std::string const &);
std::string get_random_agent();
void normalize_paths(std::string &str);
void replace_special_chars(std::string &str);
void remove_file(std::string &filename);
bool is_valid_number(std::string_view const, std::string &);
std::string view_to_string(boost::string_view const &str_view);
std::string_view bv2sv(boost::string_view);

template <typename T> threadsafe_cv_container<T> &get_scheduled_tasks() {
  static threadsafe_cv_container<T> scheduled_tasks{};
  return scheduled_tasks;
}

std::size_t timet_to_string(std::string &, std::size_t,
                            char const * = "%Y-%m-%d %H:%M:%S");
char get_random_char();
std::string get_random_string(std::size_t);
std::size_t get_random_integer();
bool create_file_directory(std::filesystem::path const &path);
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delimeter);
} // namespace utilities
} // namespace wudi_server
