#pragma once

#include "fields_alloc.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

std::vector<uint32_t> operator+(std::vector<uint32_t> const &a,
                                std::vector<uint32_t> const &b);

namespace wudi_server {
using nlohmann::json;

struct task_result_t;
struct atomic_task_t;
struct website_result_t;
struct upload_result_t;

void to_json(json &j, task_result_t const &);
void to_json(json &j, atomic_task_t const &);
void to_json(json &j, website_result_t const &);
void to_json(json &j, upload_result_t const &item);

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using url_query = std::map<boost::string_view, boost::string_view>;
using callback_t = std::function<void(http::request<http::string_body> const &,
                                      url_query const &)>;
struct rule_t {
  std::size_t num_verbs_{};
  std::array<http::verb, 3> verbs_{};
  callback_t route_callback_;

  rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback);
};

class endpoint_t {
  std::map<std::string, rule_t> endpoints;
  using rule_iterator = std::map<std::string, rule_t>::iterator;

public:
  void add_endpoint(std::string const &, std::initializer_list<http::verb>,
                    callback_t &&);
  std::optional<endpoint_t::rule_iterator> get_rules(std::string const &target);
  std::optional<rule_iterator> get_rules(boost::string_view const &target);
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

using string_response = http::response<http::string_body>;
using string_request = http::request<http::string_body>;
using dynamic_request = http::request_parser<http::string_body>;
using url_query = std::map<boost::string_view, boost::string_view>;
using nlohmann::json;

class session {
  using dynamic_body_ptr = std::unique_ptr<dynamic_request>;
  using string_body_ptr =
      std::unique_ptr<http::request_parser<http::string_body>>;
  using alloc_t = fields_alloc<char>;

  asio::io_context &io_context_;
  beast::tcp_stream tcp_stream_;
  beast::flat_buffer buffer_{};
  std::optional<http::request_parser<http::empty_body>> empty_body_parser_{};
  dynamic_body_ptr dynamic_body_parser{nullptr};
  string_body_ptr client_request_{};
  boost::string_view content_type_{};
  std::shared_ptr<void> resp_;
  endpoint_t endpoint_apis_;
  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      file_response_;
  alloc_t alloc_{8192};
  // The file-based response serializer.
  std::optional<
      http::response_serializer<http::file_body, http::basic_fields<alloc_t>>>
      file_serializer_;

private:
  std::vector<uint32_t>
  stop_running_tasks_impl(std::vector<uint32_t> const &tasks,
                          bool saving_state = true);
  void delete_stopped_tasks_impl(std::vector<uint32_t> const &tasks);
  void delete_other_tasks_impl(boost::string_view const,
                               std::vector<uint32_t> const &tasks);
  std::filesystem::path copy_file_n(std::filesystem::path const &source,
                                    std::filesystem::path const &dest,
                                    std::string const &filename,
                                    json::number_integer_t const from = 0,
                                    json::number_integer_t const to = 0);

private:
  session *shared_from_this() { return this; }
  void add_endpoint_interfaces();
  void http_read_data();
  void on_header_read(beast::error_code, std::size_t const);
  void binary_data_read(beast::error_code ec, std::size_t bytes_transferred);
  void on_data_read(beast::error_code ec, std::size_t const);
  void shutdown_socket();
  void send_response(string_response &&response);
  void error_handler(string_response &&response, bool close_socket = false);
  void on_data_written(beast::error_code ec, std::size_t const bytes_written);
  void login_handler(string_request const &, url_query const &);
  void index_page_handler(string_request const &, url_query const &);
  void upload_handler(string_request const &, url_query const &);
  void software_update_handler(string_request const &, url_query const &);
  void handle_requests(string_request const &request);
  void download_handler(string_request const &request,
                        url_query const &optional_query);
  void schedule_task_handler(string_request const &request,
                             url_query const &query);
  void website_handler(string_request const &, url_query const &);
  void get_file_handler(string_request const &, url_query const &);
  void stop_tasks_handler(string_request const &, url_query const &);
  void restart_tasks_handler(string_request const &, url_query const &);
  void remove_tasks_handler(string_request const &, url_query const &);
  void proxy_config_handler(string_request const &, url_query const &);

  static string_response json_success(json const &body,
                                      string_request const &req);
  static string_response success(char const *message, string_request const &);
  static string_response bad_request(std::string const &message,
                                     string_request const &);
  static string_response not_found(string_request const &);
  static string_response upgrade_required(string_request const &);
  static string_response method_not_allowed(string_request const &request);
  static string_response successful_login(int const id, int const role,
                                          string_request const &req);
  static string_response server_error(std::string const &, error_type_e,
                                      string_request const &);
  static string_response get_error(std::string const &, error_type_e,
                                   http::status, string_request const &);
  static url_query split_optional_queries(boost::string_view const &args);
  template <typename Func>
  void send_file(std::filesystem::path const &, std::string_view,
                 string_request const &, Func &&func);

public:
  session(asio::io_context &io, asio::ip::tcp::socket &&socket);
  bool is_closed();
  void run();
};

template <typename Func>
void session::send_file(std::filesystem::path const &file_path,
                        std::string_view const content_type,
                        string_request const &request, Func &&func) {
  std::error_code ec_{};
  if (!std::filesystem::exists(file_path, ec_)) {
    return error_handler(bad_request("file does not exist", request));
  }
  http::file_body::value_type file;
  beast::error_code ec{};
  file.open(file_path.string().c_str(), beast::file_mode::read, ec);
  if (ec) {
    return error_handler(server_error("unable to open file specified",
                                      error_type_e::ServerError, request));
  }
  file_response_.emplace(std::piecewise_construct, std::make_tuple(),
                         std::make_tuple(alloc_));
  file_response_->result(http::status::ok);
  file_response_->keep_alive(request.keep_alive());
  file_response_->set(http::field::server, "wudi-server");
  file_response_->set(http::field::content_type, content_type);
  file_response_->body() = std::move(file);
  file_response_->prepare_payload();
  file_serializer_.emplace(*file_response_);
  http::async_write(tcp_stream_, *file_serializer_,
                    [func, self = shared_from_this()](
                        beast::error_code ec, std::size_t const size_written) {
                      self->file_serializer_.reset();
                      self->file_response_.reset();
                      func();
                      self->on_data_written(ec, size_written);
                    });
}
} // namespace wudi_server
