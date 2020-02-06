#pragma once
#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/beast.hpp>
#include <boost/utility/string_view.hpp>
#include <deque>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>

#define OTL_BIG_INT long long
#define OTL_ODBC_MYSQL
#define OTL_STL
#ifdef _WIN32
#define OTL_ODBC_WINDOWS
#else
#define OTL_ODBC_UNIX
#endif
#define OTL_SAFE_EXCEPTION_ON
#include <otlv4/otlv4.h>

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
struct DatabaseConnector;

namespace utilities {
enum class SearchResultType {
  Registered = 0xA,
  NotRegistered = 0xB,
  Unknown = 0XF
};

enum class ErrorType {
  NoError,
  ResourceNotFound,
  RequiresUpdate,
  BadRequest,
  ServerError,
  MethodNotAllowed,
  Unauthorized
};

enum Constants {
  WorkerThreadCount = 10,
  SleepTimeoutSec = 5,
  FiftyMegabytes = 1024 * 1024 * 50
};

enum Anonymous {
  MaxRetries = 3,
  LenUserAgents = 18,
  MaxOpenSockets = 50,
  TimeoutMilliseconds = 3'000
};

struct ScheduledTask {
  uint32_t task_id{};
  uint32_t progress{};
  uint32_t scheduler_id{};
  uint32_t scheduled_dt{};
  std::vector<uint32_t> website_ids{};
  std::vector<uint32_t> number_ids{};
  std::string last_processed_number{};
};

struct AtomicTask {
  uint32_t const task_id;
  uint32_t website_id{};
  uint32_t total_numbers{};
  uint32_t processed{};
  std::vector<uint32_t> number_ids{};
};

struct command_line_interface {
  std::size_t thread_count{};
  uint16_t port{3456};
  uint16_t timeout_mins{15};
  std::string ip_address{"127.0.0.1"};
  std::string scheduled_snapshot;
  std::string launch_type{"development"};
  std::string database_config_filename{"../scripts/config/database.ini"};
};

struct DbConfig {
  std::string username;
  std::string password;
  std::string db_dns;

  operator bool() {
    return !(username.empty() && password.empty() && db_dns.empty());
  }
};

struct UploadResult {
  int32_t upload_id;
  int32_t total_numbers;
  std::string upload_date;
  std::string filename;
  std::string name_on_disk;
};

struct UploadRequest {
  boost::string_view const upload_filename;
  boost::string_view const name_on_disk;
  boost::string_view const uploader_id;
  boost::string_view const upload_date;
  std::size_t const total_numbers;
};

enum class OpStatus { Ongoing, Stopped, Erred };

struct AtomicTaskResult {
  uint32_t task_id{};
  uint32_t website_id{};
  uint32_t processed{};
  uint32_t total_numbers{};
  OpStatus operation_status{OpStatus::Stopped};

  std::filesystem::path ok_filename;
  std::filesystem::path not_ok_filename;
  std::filesystem::path unknown_filename;

  std::ofstream ok_file;
  std::ofstream not_ok_file;
  std::ofstream unknown_file;
};

struct WebsiteResult {
  int32_t id{};
  std::string address{};
  std::string alias;
};

struct ProxyAddress {
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

struct empty_container_exception : public std::runtime_error {
  empty_container_exception() : std::runtime_error("") {}
};

class number_stream {
public:
  number_stream(std::ifstream &file_stream);
  std::string get() noexcept(false);
  bool empty();

private:
  std::ifstream &input_stream;
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
      throw empty_container_exception{};
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
    if (filter(line, output))
      post_op(output);
  }
}

template <typename T>
using threadsafe_cv_container = threadsafe_container<T, std::deque<T>, true>;

otl_stream &operator>>(otl_stream &, UploadResult &);
otl_stream &operator>>(otl_stream &, WebsiteResult &);
bool operator<(AtomicTaskResult const &task_1, AtomicTaskResult const &task_2);
std::string decode_url(boost::string_view const &encoded_string);
bool is_valid_number(std::string_view const, std::string &);
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delimeter);
void to_json(json &j, UploadResult const &item);
void to_json(json &j, WebsiteResult const &);
void log_sql_error(otl_exception const &exception);
[[nodiscard]] std::string view_to_string(boost::string_view const &str_view);
[[nodiscard]] std::string intlist_to_string(std::vector<uint32_t> const &vec);
[[nodiscard]] DbConfig parse_database_file(std::string const &filename,
                                           std::string const &config_name);
[[nodiscard]] std::string_view bv2sv(boost::string_view);
std::string get_random_agent();
void background_task_executor(std::atomic_bool &stopped, std::mutex &,
                              std::shared_ptr<DatabaseConnector> &);
threadsafe_cv_container<AtomicTask> &get_scheduled_tasks();
std::multimap<uint32_t, std::shared_ptr<AtomicTaskResult>> &get_tasks_results();
std::size_t timet_to_string(std::string &, std::size_t,
                            char const * = "%Y-%m-%d %H:%M:%S");
bool read_task_file(std::string_view);
} // namespace utilities

using Callback = std::function<void(http::request<http::string_body> const &,
                                    std::string_view const &)>;

struct Rule {
  std::size_t num_verbs_{};
  std::array<http::verb, 3> verbs_{};
  Callback route_callback_;

  Rule(std::initializer_list<http::verb> &&verbs, Callback callback);
};

class Endpoint {
  std::map<std::string, Rule> endpoints;
  using iterator = std::map<std::string, Rule>::iterator;

public:
  void add_endpoint(std::string const &, std::initializer_list<http::verb>,
                    Callback &&);
  std::optional<Endpoint::iterator> get_rules(std::string const &target);
  std::optional<iterator> get_rules(boost::string_view const &target);
};

struct DatabaseConnector {
  utilities::DbConfig db_config;
  otl_connect otl_connector_;
  std::mutex db_mutex_;
  bool is_running = false;

private:
  std::string svector_to_string(std::vector<boost::string_view> const &vec);
  void keep_sql_server_busy();

public:
  static std::shared_ptr<DatabaseConnector> GetDBConnector();
  DatabaseConnector &username(std::string const &username);
  DatabaseConnector &password(std::string const &password);
  DatabaseConnector &database_name(std::string const &db_name);
  bool connect();

public:
  std::vector<utilities::WebsiteResult>
  get_websites(std::vector<uint32_t> const &ids);
  std::optional<utilities::WebsiteResult> get_website(uint32_t const id);
  bool add_website(std::string_view const address,
                   std::string_view const alias);
  bool add_task(utilities::ScheduledTask &task);
  std::pair<int, int> get_login_role(std::string_view const,
                                     std::string_view const);
  bool add_upload(utilities::UploadRequest const &upload_request);

  template <typename T>
  std::vector<utilities::UploadResult> get_uploads(std::vector<T> const &ids) {
    std::string sql_statement{};
    if (ids.empty()) {
      sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                      "name_on_disk FROM tb_uploads";
    } else {
      if constexpr (std::is_same_v<T, boost::string_view>) {
        sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                        "name_on_disk FROM tb_uploads WHERE id IN "
                        "({})"_format(svector_to_string(ids));
      } else {
        sql_statement = "SELECT id, filename, total_numbers, upload_date, "
                        "name_on_disk FROM tb_uploads WHERE id IN "
                        "({})"_format(utilities::intlist_to_string(ids));
      }
    }
    std::vector<utilities::UploadResult> result{};
    try {
      otl_stream db_stream(1'000'000, sql_statement.c_str(), otl_connector_);
      utilities::UploadResult item{};
      while (db_stream >> item) {
        result.push_back(std::move(item));
      }
    } catch (otl_exception const &e) {
      utilities::log_sql_error(e);
    }
    return result;
  }
};
} // namespace wudi_server
