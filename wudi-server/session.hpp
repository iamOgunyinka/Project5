#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <filesystem>

std::vector<uint32_t> operator+(std::vector<uint32_t> const &a,
                                std::vector<uint32_t> const &b);

namespace wudi_server {
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
  using iterator = std::map<std::string, rule_t>::iterator;

public:
  void add_endpoint(std::string const &, std::initializer_list<http::verb>,
                    callback_t &&);
  std::optional<endpoint_t::iterator> get_rules(std::string const &target);
  std::optional<iterator> get_rules(boost::string_view const &target);
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
  asio::io_context &io_context_;
  beast::tcp_stream tcp_stream_;
  beast::flat_buffer buffer_{};
  std::optional<http::request_parser<http::empty_body>> empty_body_parser_{};
  dynamic_body_ptr dynamic_body_parser{nullptr};
  string_body_ptr client_request_{};
  boost::string_view content_type_{};
  std::shared_ptr<void> resp_;
  endpoint_t endpoint_apis_;
  std::vector<std::shared_ptr<void>> websockets_;

private:
  std::vector<uint32_t>
  stop_running_tasks_impl(std::vector<uint32_t> const &tasks,
                          bool save_state = true);
  void delete_stopped_tasks_impl(std::vector<uint32_t> const &tasks);
  void delete_other_tasks_impl(boost::string_view const,
                               std::vector<uint32_t> const &tasks);
  std::filesystem::path copy_file_n(std::filesystem::path const &source,
                                    std::filesystem::path const &dest,
                                    std::string const &filename,
                                    json::number_integer_t const from = 0,
                                    json::number_integer_t const to = 0);
  std::string create_temporary_url(std::filesystem::path const &);

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
  void login_handler(string_request const &request, url_query const &query);
  void index_page_handler(string_request const &request,
                          url_query const &query);
  void upload_handler(string_request const &request,
                      url_query const &optional_query);
  void handle_requests(string_request const &request);
  void download_handler(string_request const &request,
                        url_query const &optional_query);
  void schedule_task_handler(string_request const &request,
                             url_query const &query);
  void website_handler(string_request const &, url_query const &);
  void stop_tasks_handler(string_request const &, url_query const &);
  void restart_tasks_handler(string_request const &, url_query const &);
  void remove_tasks_handler(string_request const &, url_query const &);

  static string_response json_success(json const &body,
                                      string_request const &req);
  static string_response success(char const *message, string_request const &);
  static string_response bad_request(std::string const &message,
                                     string_request const &);
  static string_response not_found(string_request const &);
  static string_response method_not_allowed(string_request const &request);
  static string_response successful_login(int const id, int const role,
                                          string_request const &req);
  static string_response server_error(std::string const &, error_type_e,
                                      string_request const &);
  static string_response get_error(std::string const &, error_type_e,
                                   http::status, string_request const &);
  static url_query split_optional_queries(boost::string_view const &args);

public:
  session(asio::io_context &io, asio::ip::tcp::socket &&socket);
  void run();
};
} // namespace wudi_server
