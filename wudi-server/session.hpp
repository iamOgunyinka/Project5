#pragma once

#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>

#define private_functions private

namespace wudi_server {
namespace asio = boost::asio;
namespace beast = boost::beast;
using utilities::command_line_interface;
using string_response = http::response<http::string_body>;
using string_request = http::request<http::string_body>;
using dynamic_request = http::request_parser<http::string_body>;
using nlohmann::json;
using utilities::ErrorType;
using string_view_pair = std::pair<boost::string_view, boost::string_view>;
using string_view_pair_list = std::vector<string_view_pair>;

class session {
  using dynamic_body_ptr = std::unique_ptr<dynamic_request>;
  using string_body_ptr =
      std::unique_ptr<http::request_parser<http::string_body>>;

  beast::tcp_stream tcp_stream_;
  command_line_interface const &args_;
  beast::flat_buffer buffer_{};
  std::unique_ptr<http::request_parser<http::empty_body>> empty_body_parser_{};
  dynamic_body_ptr dynamic_body_parser{nullptr};
  string_body_ptr client_request_{};
  boost::string_view content_type_{};
  std::shared_ptr<void> resp_;
  Endpoint endpoint_apis_;
  std::shared_ptr<DatabaseConnector> db_connector;

  private_functions : void add_endpoint_interfaces();
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
  session *shared_from_this() { return this; }

  static string_response json_success(json const &body,
                                      string_request const &req);
  static string_response success(char const *message, string_request const &);
  static string_response bad_request(std::string const &message,
                                     string_request const &);
  static string_response not_found(string_request const &);
  static string_response method_not_allowed(string_request const &request);
  static string_response successful_login(int const id, int const role,
                                          string_request const &req);
  static string_response server_error(std::string const &, ErrorType,
                                      string_request const &);
  static string_response get_error(std::string const &, ErrorType, http::status,
                                   string_request const &);
  static string_view_pair_list
  split_optional_queries(std::string_view const &args);

public:
  session(asio::ip::tcp::socket &&socket, command_line_interface const &args,
          std::shared_ptr<DatabaseConnector>);
  void schedule_task_handler(string_request const &request,
                             std::string_view const &query);
  void website_handler(string_request const &, std::string_view const &);
  void run();
};
} // namespace wudi_server

#undef private_functions
