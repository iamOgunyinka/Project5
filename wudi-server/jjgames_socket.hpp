#pragma once

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace wudi_server {

namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using utilities::proxy_address_t;
using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using utilities::search_result_type_e;
using utilities::task_status_e;

class jjgames_socket {
  static std::string jjgames_hostname;
  enum class type_sent_e { Normal, GetHash, SetHash };
  type_sent_e type_ = type_sent_e::Normal;

private:
  net::io_context &io_;
  utilities::number_stream_t &numbers_;
  proxy_provider_t &proxy_provider_;
  beast::ssl_stream<beast::tcp_stream> ssl_stream_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  endpoint_ptr current_proxy_{nullptr};

  std::size_t success_sent_count_{};
  std::size_t handshake_retries_{};
  std::vector<uint8_t> handshake_buffer{};
  char reply_buffer[512]{};

private:
  void get_form_hash();
  void prepare_hash_request();
  void process_gethash_response(std::string const &body);
  void process_sethash_response();
  void process_normal_response(std::string const &body);
  void send_first_request();

private:
  void perform_socks5_handshake();
  void perform_ssl_handshake();
  void set_authentication_header();
  void on_ssl_handshake(beast::error_code);
  void on_first_handshake_initiated(beast::error_code, std::size_t const);
  void read_socks5_server_response(bool const);
  void on_handshake_response_received(beast::error_code, bool const);
  void perform_sock5_second_handshake();
  void retry_first_handshake();

  void close_socket();
  void connect();
  void receive_data();
  void reconnect();
  void resend_http_request();
  void choose_next_proxy();
  void send_http_data();
  void on_data_sent(beast::error_code, std::size_t const);
  void current_proxy_assign_prop(ProxyProperty);
  void prepare_request_data(bool use_auth = false);
  void on_connected(beast::error_code,
                    tcp::resolver::results_type::endpoint_type);
  void send_next();
  void on_data_received(beast::error_code, std::size_t const);

public:
  jjgames_socket(bool &stopped, net::io_context &, proxy_provider_t &,
                 utilities::number_stream_t &, net::ssl::context &);
  void start_connect();
  ~jjgames_socket() { close_socket(); }
  auto &signal() { return signal_; }
};

} // namespace wudi_server
