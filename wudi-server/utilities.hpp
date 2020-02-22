#pragma once
#include <array>
#include <boost/algorithm/string.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
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
#include <string>
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
namespace http = boost::beast::http;
namespace net = boost::asio;

using nlohmann::json;
using namespace fmt::v6::literals;
struct database_connector_t;

namespace utilities {
enum class search_result_type_e {
  Registered = 0xA,
  NotRegistered = 0xB,
  Unknown = 0XF
};

enum class error_type_e {
  NoError,
  ResourceNotFound,
  RequiresUpdate,
  BadRequest,
  ServerError,
  MethodNotAllowed,
  Unauthorized
};

enum constants_e {
  WorkerThreadCount = 10,
  SleepTimeoutSec = 5,
  FiveMegabytes = 1024 * 1024 * 5
};

enum anonymous_e {
  MaxRetries = 2,
  LenUserAgents = 18,
  MaxOpenSockets = 50,
  TimeoutMilliseconds = 3'000
};

struct scheduled_task_t {
  uint32_t task_id{};
  uint32_t progress{};
  uint32_t scheduler_id{};
  uint32_t scheduled_dt{};
  uint64_t total_numbers{};
  std::vector<uint32_t> website_ids{};
  std::vector<uint32_t> number_ids{};
  std::string last_processed_number{};
};

struct task_result_t {
  int task_status;
  uint32_t total_numbers;
  uint32_t id;
  uint32_t progress;
  std::string data_ids;
  std::string website_ids;
  std::string scheduled_date;
};

struct atomic_task_t {
  enum class task_type { stopped, fresh };
  struct fresh_task {
    uint32_t website_id{};
    std::vector<uint32_t> number_ids{};
  };
  struct stopped_task {
    std::string input_filename{};
    std::string website_address{};
    std::string ok_filename{};
    std::string not_ok_filename{};
    std::string unknown_filename{};
  };

  task_type type_ = task_type::fresh;
  uint32_t task_id{};
  uint32_t website_id{};
  uint32_t processed{};
  uint32_t total{};
  std::variant<fresh_task, stopped_task> task;
};

struct command_line_interface {
  std::size_t thread_count{};
  uint16_t port{80};
  uint16_t timeout_mins{15};
  std::string ip_address{"127.0.0.1"};
  std::string scheduled_snapshot;
  std::string launch_type{"development"};
  std::string database_config_filename{"../scripts/config/database.ini"};
};

struct upload_request_t {
  boost::string_view const upload_filename;
  boost::string_view const name_on_disk;
  boost::string_view const uploader_id;
  boost::string_view const upload_date;
  std::size_t const total_numbers;
};

struct upload_result_t {
  int32_t upload_id;
  int32_t total_numbers;
  std::string upload_date;
  std::string filename;
  std::string name_on_disk;
};

enum class task_status_e : uint32_t {
  NotStarted,
  Ongoing,
  Stopped,
  Erred,
  Completed
};

using string_view_pair = std::pair<boost::string_view, boost::string_view>;
using string_view_pair_list = std::vector<string_view_pair>;

class atomic_task_result_t {
  boost::signals2::signal<void(uint32_t, uint32_t, task_status_e)>
      progress_signal_;
  bool stopped_ = false;

public:
  uint32_t task_id{};
  uint32_t website_id{};
  uint32_t processed{};
  uint32_t total_numbers{};
  task_status_e operation_status{task_status_e::NotStarted};

  std::filesystem::path ok_filename;
  std::filesystem::path not_ok_filename;
  std::filesystem::path unknown_filename;

  std::ofstream ok_file;
  std::ofstream not_ok_file;
  std::ofstream unknown_file;

  boost::signals2::signal<void(uint32_t, uint32_t, task_status_e)> &
  progress_signal();
  bool &stopped();
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
  static std::array<char const *, LenUserAgents> const user_agents;
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
  decltype(std::declval<std::ifstream>().rdbuf()) dump();

private:
  std::ifstream &input_stream;
};

template <typename Key, typename Value> class custom_map {
  std::map<Key, Value> map_{};

public:
  custom_map() = default;

  bool contains(Key const &key) { return map_.find(key) != map_.cend(); }

  std::optional<Value> value(Key const &key) {
    if (auto iter = map_.find(key); iter == map_.cend())
      return std::nullopt;
    else
      return iter->second;
  }

  void insert(Key key, Value const &value) {
    map_.emplace(std::forward<Key>(key), value);
  }

  void clear() {
    for (auto &data : map_) {
      boost::beast::error_code ec{};
      if (data.second && data.second->is_open())
        data.second->close(ec);
    }
  }

  bool remove(Key const &key) {
    if (auto iter = map_.find(key); iter != map_.cend()) {
      iter->second.reset();
      map_.erase(iter);
      return true;
    }
    return false;
  }
};

template <typename Key> class sharedtask_ptr {
  std::map<Key, uint32_t> map_{};
  std::mutex mutex_;

  bool contains(Key const &key) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return map_.find(key) != map_.cend();
  }

public:
  sharedtask_ptr() = default;

  template <typename Callable> void insert(Key key, Callable function) {
    if (!contains(key)) {
      function(key);
    }
    std::lock_guard<std::mutex> lock_g{mutex_};
    ++map_[key];
  }

  template <typename Callable> bool remove(Key const &key, Callable &&func) {
    if (!contains(key))
      return false;
    std::lock_guard<std::mutex> lock_g{mutex_};
    auto &value = map_[key];
    --value;
    if (value > 0)
      return true;
    map_.erase(key);
    func(key);
    return true;
  }
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

bool operator<(atomic_task_result_t const &task_1,
               atomic_task_result_t const &task_2);
std::string svector_to_string(std::vector<boost::string_view> const &vec);
std::string decode_url(boost::string_view const &encoded_string);
bool is_valid_number(std::string_view const, std::string &);
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delimeter);
void to_json(json &j, upload_result_t const &item);
void to_json(json &j, website_result_t const &);
void to_json(json &j, task_result_t const &);
[[nodiscard]] std::string view_to_string(boost::string_view const &str_view);
[[nodiscard]] std::string
intlist_to_string(std::vector<atomic_task_t> const &vec);
[[nodiscard]] std::string intlist_to_string(std::vector<uint32_t> const &vec);
[[nodiscard]] std::string_view bv2sv(boost::string_view);
std::string get_random_agent();

threadsafe_cv_container<atomic_task_t> &get_scheduled_tasks();

std::multimap<uint32_t, std::shared_ptr<atomic_task_result_t>> &
get_response_queue();

sharedtask_ptr<uint32_t> &get_task_counter();

std::size_t timet_to_string(std::string &, std::size_t,
                            char const * = "%Y-%m-%d %H:%M:%S");
bool read_task_file(std::string_view);
string_view_pair_list::const_iterator
find_query_key(string_view_pair_list const &, boost::string_view const &);
} // namespace utilities

using callback_t = std::function<void(http::request<http::string_body> const &,
                                      std::string_view const &)>;

struct rule_t {
  std::size_t num_verbs_{};
  std::array<http::verb, 3> verbs_{};
  callback_t route_callback_;

  rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback);
};

class endpoint_t {
  std::map<std::string, rule_t> endpoints;
  using iterator = std::map<std::string, rule_t>::iterator;

public:
  void add_endpoint(std::string const &, std::initializer_list<http::verb>,
                    callback_t &&);
  std::optional<endpoint_t::iterator> get_rules(std::string const &target);
  std::optional<iterator> get_rules(boost::string_view const &target);
};
} // namespace wudi_server
