#pragma once
#include <array>
#include <boost/algorithm/string.hpp>
#include <boost/signals2.hpp>
#include <boost/utility/string_view.hpp>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

namespace wudi_server {
using nlohmann::json;
struct database_connector_t;

namespace utilities {
enum class task_status_e {
  NotStarted,
  Ongoing,
  Stopped,
  Erred,
  Completed,
  AutoStopped
};

enum constants_e {
  SleepTimeoutSec = 5,
  LenUserAgents = 14,
};

struct scheduled_task_t {
  uint32_t task_id{};
  uint32_t scans_per_ip{};
  uint32_t progress{};
  uint32_t scheduler_id{};
  uint32_t scheduled_dt{};
  uint32_t total_numbers{};
  uint32_t website_id{};
  std::vector<uint32_t> number_ids{};
};

struct task_result_t {
  int task_status{};
  uint32_t id{};
  uint32_t total{};
  uint32_t ok{};
  uint32_t not_ok{};
  uint32_t unknown{};
  uint32_t processed{};
  uint32_t website_id{};
  uint32_t scans_per_ip{};
  uint32_t ip_used{};
  std::string data_ids{};
  std::string scheduled_date{};
};

struct atomic_task_t {
  enum task_type { stopped, fresh, completed };

  int type_ = task_type::fresh;
  uint32_t task_id{};
  uint32_t scans_per_ip{};
  uint32_t ip_used{};
  uint32_t website_id{};
  uint32_t processed{};
  uint32_t total{};
  uint32_t ok_count{};
  uint32_t not_ok_count{};
  uint32_t unknown_count{};
  std::string input_filename{};
  std::string ok_filename{};
  std::string ok2_filename{};
  std::string not_ok_filename{};
  std::string unknown_filename{};
  std::string website_address{};
  std::vector<uint32_t> number_ids{};
};

struct upload_request_t {
  boost::string_view upload_filename;
  boost::string_view name_on_disk;
  boost::string_view uploader_id;
  boost::string_view upload_date;
  std::size_t total_numbers;
};

struct upload_result_t {
  int32_t upload_id{};
  int32_t total_numbers{};
  int32_t status{};
  std::string upload_date;
  std::string filename;
  std::string name_on_disk;
};

class internal_task_result_t {
  bool stopped_ = false;
  bool save_state_ = true;

public:
  task_status_e operation_status{task_status_e::NotStarted};
  uint32_t task_id{};
  uint32_t website_id{};
  uint32_t ok_count{};
  uint32_t not_ok_count{};
  uint32_t unknown_count{};
  uint32_t processed{};
  uint32_t total_numbers{};
  uint32_t scans_per_ip{};
  uint32_t ip_used{};

  std::filesystem::path ok_filename;
  std::filesystem::path ok2_filename;
  std::filesystem::path not_ok_filename;
  std::filesystem::path unknown_filename;
  std::ofstream ok_file;
  std::ofstream ok2_file;
  std::ofstream not_ok_file;
  std::ofstream unknown_file;

  bool &stopped();
  bool &saving_state();
  void stop();
};

struct website_result_t {
  int32_t id{};
  std::string address{};
  std::string alias{};
};

struct request_handler {
  static std::array<char const *, constants_e::LenUserAgents> const user_agents;
};

struct uri {
  uri(std::string const &url_s);
  std::string path() const;
  std::string host() const;
  std::string target() const;
  std::string protocol() const;

private:
  void parse(std::string const &);
  std::string host_;
  std::string path_;
  std::string protocol_;
  std::string query_;
};

template <typename T, typename Container> struct threadsafe_container_t {
private:
  std::mutex mutex_{};
  Container container_{};
  std::size_t total_{};
  std::condition_variable cv_{};

public:
  threadsafe_container_t(Container &&container)
      : container_{std::move(container)}, total_{container_.size()} {}
  threadsafe_container_t() = default;

  threadsafe_container_t(threadsafe_container_t &&vec)
      : mutex_{std::move(vec.mutex_)}, container_{std::move(vec.container_)},
        total_{vec.total_}, cv_{std::move(vec.cv_)} {}
  threadsafe_container_t &operator=(threadsafe_container_t &&) = delete;
  threadsafe_container_t(threadsafe_container_t const &) = delete;
  threadsafe_container_t &operator=(threadsafe_container_t const &) = delete;

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
    boost::trim(line);
    if (filter(line, output))
      post_op(output);
  }
}

template <typename T>
using threadsafe_cv_container = threadsafe_container_t<T, std::deque<T>>;

std::vector<atomic_task_t> restart_tasks(std::vector<uint32_t> const &task_ids);
std::string md5(std::string const &);
std::string get_random_agent();
void normalize_paths(std::string &str);
void replace_special_chars(std::string &str);
void remove_file(std::string &filename);
std::string svector_to_string(std::vector<boost::string_view> const &vec);
std::string decode_url(boost::string_view const &encoded_string);
bool is_valid_number(std::string_view const, std::string &);
void to_json(json &j, task_result_t const &);
void to_json(json &j, atomic_task_t const &);
void to_json(json &j, website_result_t const &);
void to_json(json &j, upload_result_t const &item);
std::string view_to_string(boost::string_view const &str_view);
std::string intlist_to_string(std::vector<atomic_task_t> const &vec);
std::string intlist_to_string(std::vector<uint32_t> const &vec);
std::string_view bv2sv(boost::string_view);
threadsafe_cv_container<atomic_task_t> &get_scheduled_tasks();
std::map<uint32_t, std::shared_ptr<internal_task_result_t>> &
get_response_queue();
std::size_t timet_to_string(std::string &, std::size_t,
                            char const * = "%Y-%m-%d %H:%M:%S");
char get_random_char();
std::string get_random_string(std::size_t);
std::size_t get_random_integer();
bool create_file_directory(std::filesystem::path const &path);
std::time_t &proxy_fetch_interval();
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delimeter);
bool operator<(internal_task_result_t const &task_1,
               internal_task_result_t const &task_2);
} // namespace utilities
} // namespace wudi_server
