#pragma once
#include "socks5_https_socket_base.hpp"

/*
#include "number_stream.hpp"
#include "safe_proxy.hpp"
#include "sockets_interface.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <optional>
*/
namespace wudi_server {
/*
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = net::ssl;

using tcp = boost::asio::ip::tcp;
using beast::error_code;

class custom_sk5_https_t : public sockets_interface {
  net::io_context &io_;
  ssl::context &ssl_context_;
  proxy_base_t &proxy_provider_;
  number_stream_t &numbers_;

  std::optional<beast::ssl_stream<beast::tcp_stream>> ssl_stream_;
  std::optional<beast::flat_buffer> general_buffer_{};
  std::vector<char> reply_buffer{};
  std::vector<char> handshake_buffer{};
  std::size_t connect_count_{};
  int const scans_per_ip_;
  bool &stopped_;

protected:
  proxy_base_t::value_type current_proxy_{nullptr};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};

protected:
  void send_first_request();
  void perform_socks5_handshake();
  void on_first_handshake_initiated(error_code, std::size_t const);
  void read_first_handshake_result();
  void on_first_handshake_response_received(error_code, std::size_t const);
  void perform_sock5_second_handshake();
  void on_auth_response_received(error_code, std::size_t);
  void process_ipv4_response(error_code, std::size_t);
  void on_handshake_response_received(error_code, std::size_t);
  void read_socks5_server_response();
  void set_authentication_header();
  void perform_ssl_handshake();
  void on_ssl_handshake(error_code);

  void close_socket();
  void close_stream();
  void perform_ssl_ritual();
  void connect();
  void receive_data();
  void reconnect();
  void choose_next_proxy(bool first_request = false);
  void send_https_data();
  void on_data_sent(error_code, std::size_t const);
  void current_proxy_assign_prop(proxy_property_e);
  void on_connected(error_code);
  void send_next();

  virtual void on_data_received(error_code, std::size_t const) = 0;
  virtual std::string hostname() const = 0;
  virtual void prepare_request_data(bool use_auth = false) = 0;
  virtual uint16_t port() const { return 443; }

public:
  custom_sk5_https_t(net::ssl::context &, bool &, net::io_context &,
                     proxy_base_t &, number_stream_t &, int);
  void start_connect() override;
  virtual ~custom_sk5_https_t() {
    signal_.disconnect_all_slots();
    close_socket();
  }
};
*/

class fafa77_socks5_socket_t final
    : public socks5_https<fafa77_socks5_socket_t> {
  std::string bearer_token{};
  std::string session_cookie{};
  tcp::endpoint proxy_endpoint{};

  using custom_sk5_https_t = socks5_https<fafa77_socks5_socket_t>;

private:
  void prepare_preliminary_request();
  void extract_bearer_token(std::string const &);

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  std::string hostname() const;

  template <typename... Args>
  fafa77_socks5_socket_t(ssl::context &ssl_context, Args &&... args)
      : custom_sk5_https_t(ssl_context, std::forward<Args>(args)...) {}
  ~fafa77_socks5_socket_t() {}
};

} // namespace wudi_server
