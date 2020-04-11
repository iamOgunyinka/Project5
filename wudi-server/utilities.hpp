#pragma once
#include <array>
#include <atomic>
#include <boost/algorithm/string.hpp>
#include <boost/signals2.hpp>
#include <boost/utility/string_view.hpp>
#include <deque>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>
#include <variant>
#include <vector>

namespace fmt {
template <> struct formatter<boost::string_view> {
  template <typename ParseContext> constexpr auto parse(ParseContext &ctxt) {
    return ctxt.begin();
  }

  template <typename FormatContext>
  auto format(boost::string_view const &view, FormatContext &ctxt) {
    return format_to(ctxt.out(), "{}",
                     std::string_view{view.data(), view.size()});
  }
};

template <> struct formatter<std::vector<int32_t>> {
  template <typename ParseContext> constexpr auto parse(ParseContext &ctxt) {
    return ctxt.begin();
  }

  template <typename FormatContext>
  auto format(std::vector<int32_t> const &integer_list, FormatContext &ctx) {
    if (integer_list.empty()) {
      return format_to(ctx.out(), "");
    }
    std::ostringstream stream{};
    for (std::size_t i = 0; i < integer_list.size() - 1; ++i) {
      stream << integer_list[i] << ", ";
    }
    stream << integer_list.back();
    return format_to(ctx.out(), "{}", stream.str());
  }
};
} // namespace fmt

namespace wudi_server {
using nlohmann::json;
using namespace fmt::v6::literals;
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

enum class search_result_type_e {
  Registered = 0xA,
  NotRegistered = 0xB,
  Unknown = 0XC,
  RequestStop = 0xD,
  Registered2 = 0xE // only for PPSports
};

enum constants_e {
  MaxRetries = 2,
  SleepTimeoutSec = 5,
  LenUserAgents = 14,
  TimeoutMilliseconds = 3'000
};

struct scheduled_task_t {
  uint32_t task_id{};
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
  std::string data_ids{};
  std::string scheduled_date{};
};

struct atomic_task_t {
  enum task_type { stopped, fresh, completed };

  int type_ = task_type::fresh;
  uint32_t task_id{};
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

struct proxy_address_t {
  std::string ip{};
  std::string port{};
};

struct request_handler {
  static std::array<char const *, constants_e::LenUserAgents> const user_agents;
};

struct uri {
  uri(std::string const &url_s);
  std::string path() const;
  std::string host() const;

private:
  void parse(std::string const &);
  std::string host_;
  std::string path_;
  std::string protocol_;
  std::string query_;
};

struct empty_container_exception_t : public std::runtime_error {
  empty_container_exception_t() : std::runtime_error("") {}
};

class number_stream_t {
public:
  number_stream_t(std::ifstream &file_stream);
  std::string get() noexcept(false);
  bool empty();
  bool is_open();
  void close();
  decltype(std::declval<std::ifstream>().rdbuf()) dump_s();
  std::vector<std::string> &dump();
  void push_back(std::string const &);

private:
  std::ifstream &input_stream;
  std::vector<std::string> temporaries_;
  std::mutex mutex_;
  bool closed_ = false;
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
      throw empty_container_exception_t{};
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
    T value = container_.front();
    container_.pop_front();
    total_ = container_.size();
    return value;
  }

  template <typename U, typename Func>
  std::vector<T> remove_task(U &&keys, Func &&function) {
    if (container_.empty())
      return {};
    std::unique_lock<std::mutex> u_lock{mutex_};
    std::vector<T> result{};
    for (auto &task : container_) {
      if (function(task, keys))
        result.emplace_back(std::move(task));
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
    boost::trim(line);
    if (filter(line, output))
      post_op(output);
  }
}

template <typename T>
using threadsafe_cv_container = threadsafe_container<T, std::deque<T>, true>;

std::vector<atomic_task_t> restart_tasks(std::vector<uint32_t> const &task_ids);
std::string get_random_agent();
std::string get_random_ip_address();
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
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delimeter);
bool operator<(internal_task_result_t const &task_1,
               internal_task_result_t const &task_2);
} // namespace utilities
} // namespace wudi_server
