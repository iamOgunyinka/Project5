#pragma once

#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <optional>

std::vector<uint32_t> operator+(std::vector<uint32_t> const &a,
                                std::vector<uint32_t> const &b);

namespace wudi_server {
namespace asio = boost::asio;
namespace beast = boost::beast;
using utilities::command_line_interface;
using string_response = http::response<http::string_body>;
using string_request = http::request<http::string_body>;
using dynamic_request = http::request_parser<http::string_body>;
using nlohmann::json;
using utilities::error_type_e;

class session {
  using dynamic_body_ptr = std::unique_ptr<dynamic_request>;
  using string_body_ptr =
      std::unique_ptr<http::request_parser<http::string_body>>;
  net::io_context &io_context_;
  beast::tcp_stream tcp_stream_;
  command_line_interface const &args_;
  beast::flat_buffer buffer_{};
  std::optional<http::request_parser<http::empty_body>> empty_body_parser_{};
  dynamic_body_ptr dynamic_body_parser{nullptr};
  string_body_ptr client_request_{};
  boost::string_view content_type_{};
  std::shared_ptr<void> resp_;
  endpoint_t endpoint_apis_;
  std::vector<std::shared_ptr<void>> websockets_;

private:
  std::vector<uint32_t> stop_running_tasks_impl(json::array_t const &tasks);
  std::vector<uint32_t> delete_completed_tasks_impl(json::array_t const &tasks);
  std::vector<uint32_t> delete_stopped_tasks_impl(json::array_t const &tasks);

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
  void login_handler(string_request const &request,
                     std::string_view const &query);
  void index_page_handler(string_request const &request,
                          std::string_view const &query);
  void upload_handler(string_request const &request,
                      std::string_view const &optional_query);
  void handle_requests(string_request const &request);
  void download_handler(string_request const &request,
                        std::string_view const &optional_query);
  void schedule_task_handler(string_request const &request,
                             std::string_view const &query);
  void website_handler(string_request const &, std::string_view const &);
  void stop_tasks_handler(string_request const &, std::string_view const &);
  void restart_tasks_handler(string_request const &, std::string_view const &);
  void remove_tasks_handler(string_request const &, std::string_view const &);

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
  static utilities::string_view_pair_list
  split_optional_queries(std::string_view const &args);

public:
  session(net::io_context &io, asio::ip::tcp::socket &&socket,
          command_line_interface const &args);
  void run();
};
} // namespace wudi_server
