#pragma once

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <optional>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = net::ssl;

using utilities::proxy_address_t;
using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using utilities::search_result_type_e;

template <typename Derived> class socks5_https_socket_base_t {
protected:
  net::io_context &io_;
  utilities::number_stream_t &numbers_;
  proxy_provider_t &proxy_provider_;
  ssl::context &ssl_context_;
  beast::ssl_stream<beast::tcp_stream> ssl_stream_;
  bool &stopped_;

  std::vector<char> socks_read_buffer_{};

  std::optional<beast::flat_buffer> general_buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  endpoint_ptr current_proxy_{nullptr};

  std::size_t handshake_retries_{};
  std::size_t second_handshake_retries_{};
  std::vector<uint8_t> handshake_buffer{};

protected:
  void send_first_request();
  void perform_socks5_handshake();
  void perform_ssl_handshake();
  void set_authentication_header();
  void on_ssl_handshake(beast::error_code);
  void on_first_handshake_initiated(beast::error_code, std::size_t const);
  void read_socks5_server_response(bool const);
  void on_handshake_received(beast::error_code, std::size_t, bool const);
  void perform_sock5_second_handshake();
  void retry_first_handshake();
  void retry_second_handshake();

  void close_socket();
  void connect();
  void receive_data();
  void reconnect();
  void resend_http_request();
  void choose_next_proxy(bool first_request = false);
  void send_https_data();
  void on_data_sent(beast::error_code, std::size_t const);
  void current_proxy_assign_prop(ProxyProperty);
  void prepare_request_data(bool use_auth = false);
  void on_connected(beast::error_code,
                    tcp::resolver::results_type::endpoint_type);
  virtual void send_next();
  void on_data_received(beast::error_code, std::size_t const);
  std::string hostname() const;

public:
  socks5_https_socket_base_t(bool &stopped, net::io_context &,
                             proxy_provider_t &, utilities::number_stream_t &,
                             net::ssl::context &);
  void start_connect();
  ~socks5_https_socket_base_t() {
    signal_.disconnect_all_slots();
    close_socket();
  }
  auto &signal() { return signal_; }
};

template <typename Derived>
socks5_https_socket_base_t<Derived>::socks5_https_socket_base_t(
    bool &stopped, net::io_context &io_context,
    proxy_provider_t &proxy_provider, utilities::number_stream_t &numbers,
    ssl::context &ssl_context)
    : io_{io_context}, ssl_stream_{net::make_strand(io_), ssl_context},
      numbers_{numbers}, proxy_provider_{proxy_provider},
      ssl_context_{ssl_context}, stopped_{stopped} {}

template <typename Derived>
std::string socks5_https_socket_base_t<Derived>::hostname() const {
  return static_cast<Derived const *>(this)->hostname();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::on_connected(
    beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    return reconnect();
  }
  return perform_socks5_handshake();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::perform_socks5_handshake() {
  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // method count
  handshake_buffer.push_back(0x00); // first method

  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(5'000));
  beast::get_lowest_layer(ssl_stream_)
      .async_write_some(
          net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
          std::bind(&socks5_https_socket_base_t::on_first_handshake_initiated,
                    this, std::placeholders::_1, std::placeholders::_2));
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::retry_first_handshake() {
  ++handshake_retries_;
  if (handshake_retries_ >= utilities::MaxRetries) {
    handshake_retries_ = 0;
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  perform_socks5_handshake();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::retry_second_handshake() {
  ++second_handshake_retries_;
  if (second_handshake_retries_ >= utilities::MaxRetries) {
    second_handshake_retries_ = 0;
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  perform_sock5_second_handshake();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::on_first_handshake_initiated(
    beast::error_code const ec, std::size_t const) {
  if (ec) { // could be timeout
    return retry_first_handshake();
  }
  return read_socks5_server_response(true);
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::read_socks5_server_response(
    bool const is_first_handshake) {
  socks_read_buffer_.clear();
  socks_read_buffer_.resize(25, 0);
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(
          std::chrono::milliseconds(utilities::TimeoutMilliseconds * 3));
  beast::get_lowest_layer(ssl_stream_)
      .async_read_some(net::mutable_buffer(socks_read_buffer_.data(),
                                           socks_read_buffer_.size()),
                       [=](beast::error_code ec, std::size_t const s) {
                         on_handshake_received(ec, s, is_first_handshake);
                       });
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::on_handshake_received(
    beast::error_code ec, std::size_t const sz, bool const is_first_handshake) {
  if (ec) {
    if (is_first_handshake) {
      return retry_first_handshake();
    }
    return retry_second_handshake();
  }
  char const *p1 = reinterpret_cast<char *>(socks_read_buffer_.data());
  if (is_first_handshake) {
    if (sz > 1 && p1 && p1[1] != 0x00) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      return choose_next_proxy();
    }
    return perform_sock5_second_handshake();
  }
  if (sz > 1 && p1 && p1[1] != 0x00) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  return perform_ssl_handshake();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::perform_ssl_handshake() {
  beast::get_lowest_layer(ssl_stream_).expires_after(std::chrono::seconds(5));
  ssl_stream_.async_handshake(
      net::ssl::stream_base::client,
      [=](beast::error_code ec) { return on_ssl_handshake(ec); });
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::on_ssl_handshake(
    beast::error_code ec) {
  if (ec.category() == net::error::get_ssl_category() &&
      ec.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ)) {
    return send_https_data();
  }
  if (ec) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  send_https_data();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::perform_sock5_second_handshake() {

  std::string const host_name = hostname();
  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // TCP/IP
  handshake_buffer.push_back(0x00); // must be 0x00 always
  handshake_buffer.push_back(0x03); // Domain=0x03. IPv4 = 0x01. IPv6=0x04
  handshake_buffer.push_back(static_cast<uint8_t>(host_name.size()));
  for (auto const &hn : host_name) {
    handshake_buffer.push_back(hn);
  }
  // host to network short(htons)
  handshake_buffer.push_back(443 >> 8);
  handshake_buffer.push_back(443 & 0xFF);

  beast::get_lowest_layer(ssl_stream_).expires_after(std::chrono::seconds(5));
  beast::get_lowest_layer(ssl_stream_)
      .async_write_some(
          net::mutable_buffer(handshake_buffer.data(), handshake_buffer.size()),
          [this](beast::error_code ec, std::size_t const) {
            if (ec) {
              current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
              return choose_next_proxy();
            }
            return read_socks5_server_response(false);
          });
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::prepare_request_data(
    bool use_authentication_header) {
  static_cast<Derived *>(this)->prepare_request_data(use_authentication_header);
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::choose_next_proxy(
    bool const is_first_request) {
  send_count_ = 0;
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    spdlog::error("error getting next endpoint");
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (!is_first_request) {
    ssl_stream_.async_shutdown([=](beast::error_code) { return connect(); });
  }
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::current_proxy_assign_prop(
    ProxyProperty property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  temp_list_ = {*current_proxy_};
  beast::get_lowest_layer(ssl_stream_)
      .async_connect(temp_list_, [=](auto const &ec, auto const &ep_type) {
        on_connected(ec, ep_type);
      });
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::send_first_request() {
  if (stopped_) {
    if (!current_number_.empty()) {
      numbers_.push_back(current_number_);
    }
    current_number_.clear();
    return;
  }
  try {
    current_number_ = numbers_.get();
    prepare_request_data();
    connect();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::receive_data() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds *
                                               4)); // 4*3secs
  response_ = {};
  general_buffer_.emplace();
  http::async_read(ssl_stream_, *general_buffer_, response_,
                   beast::bind_front_handler(
                       &socks5_https_socket_base_t::on_data_received, this));
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::start_connect() {
  if (!SSL_set_tlsext_host_name(ssl_stream_.native_handle(),
                                hostname().c_str())) {
    beast::error_code ec{static_cast<int>(::ERR_get_error()),
                         net::error::get_ssl_category()};
    return spdlog::error("Unable to set TLS because: {}", ec.message());
  }
  ssl_stream_.set_verify_mode(net::ssl::verify_none);
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::send_https_data() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(ssl_stream_, request_,
                    beast::bind_front_handler(
                        &socks5_https_socket_base_t::on_data_sent, this));
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::on_data_sent(beast::error_code ec,
                                                       std::size_t const s) {
  if (ec) {
    resend_http_request();
  } else
    receive_data();
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  } else {
    send_https_data();
  }
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::close_socket() {
  beast::get_lowest_layer(ssl_stream_).cancel();
  beast::error_code ec{};
  ec = {};
  ssl_stream_.async_shutdown([this](beast::error_code) {
    beast::get_lowest_layer(ssl_stream_).close();
  });
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::send_next() {
  if (stopped_) {
    if (!current_number_.empty()) {
      numbers_.push_back(current_number_);
    }
    current_number_.clear();
    return;
  }
  try {
    current_number_ = numbers_.get();
    prepare_request_data();
    send_https_data();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::set_authentication_header() {
  prepare_request_data(true);
}

template <typename Derived>
void socks5_https_socket_base_t<Derived>::on_data_received(
    beast::error_code ec, std::size_t const s) {
  static_cast<Derived *>(this)->on_data_received(ec, s);
}

} // namespace wudi_server
