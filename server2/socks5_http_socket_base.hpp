#pragma once

#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <optional>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using utilities::proxy_address_t;
using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using utilities::search_result_type_e;

template <typename Derived, typename Proxy> class socks5_http_socket_base_t {
  net::io_context &io_;
  std::optional<beast::tcp_stream> tcp_stream_;

protected:
  utilities::number_stream_t &numbers_;
  Proxy &proxy_provider_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  typename Proxy::value_type current_proxy_{nullptr};

  std::size_t success_sent_count_{};
  std::size_t handshake_retries_{};
  std::size_t second_handshake_retries_{};
  std::vector<uint8_t> handshake_buffer{};
  char reply_buffer[64]{};

protected:
  void send_first_request();
  void perform_socks5_handshake();
  void set_authentication_header();
  void on_first_handshake_initiated(beast::error_code, std::size_t const);
  void read_socks5_server_response(bool const);
  void on_handshake_response_received(beast::error_code, bool const);
  void perform_sock5_second_handshake();
  void retry_first_handshake();
  void retry_second_handshake();

  void close_socket();
  void close_stream();
  void connect();
  void receive_data();
  void reconnect();
  void resend_http_request();
  void choose_next_proxy(bool is_first_request = false);
  void on_data_sent(beast::error_code, std::size_t const);
  void current_proxy_assign_prop(typename Proxy::Property);
  void prepare_request_data(bool use_auth = false);
  void on_connected(beast::error_code,
                    tcp::resolver::results_type::endpoint_type);
  void send_http_data();
  void on_data_received(beast::error_code, std::size_t const);
  std::string hostname() const;
  virtual void send_next();

public:
  socks5_http_socket_base_t(bool &, net::io_context &, Proxy &,
                            utilities::number_stream_t &);
  void start_connect();
  ~socks5_http_socket_base_t() {
    signal_.disconnect_all_slots();
    close_socket();
  }
  auto &signal() { return signal_; }
};

template <typename Derived, typename Proxy>
socks5_http_socket_base_t<Derived, Proxy>::socks5_http_socket_base_t(
    bool &stopped, net::io_context &io_context, Proxy &proxy_provider,
    utilities::number_stream_t &numbers)
    : io_{io_context}, tcp_stream_{net::make_strand(io_)}, numbers_{numbers},
      proxy_provider_{proxy_provider}, stopped_{stopped} {}

template <typename Derived, typename ProxyProvider>
std::string
socks5_http_socket_base_t<Derived, ProxyProvider>::hostname() const {
  return static_cast<Derived const *>(this)->hostname();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::on_connected(
    beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    return reconnect();
  }
  return perform_socks5_handshake();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::perform_socks5_handshake() {
  handshake_buffer.clear();
  handshake_buffer.push_back(0x05); // version
  handshake_buffer.push_back(0x01); // method count
  handshake_buffer.push_back(0x00); // first method

  tcp_stream_->expires_after(std::chrono::milliseconds(5'000));
  tcp_stream_->async_write_some(
      net::const_buffer(reinterpret_cast<char const *>(handshake_buffer.data()),
                        handshake_buffer.size()),
      std::bind(&socks5_http_socket_base_t::on_first_handshake_initiated, this,
                std::placeholders::_1, std::placeholders::_2));
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::retry_first_handshake() {
  ++handshake_retries_;
  if (handshake_retries_ >= utilities::MaxRetries) {
    handshake_retries_ = 0;
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  perform_socks5_handshake();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::close_stream() {
  tcp_stream_->close();
}

template <typename Derived, typename Proxy>
void socks5_http_socket_base_t<Derived, Proxy>::retry_second_handshake() {
  ++second_handshake_retries_;
  if (second_handshake_retries_ >= utilities::MaxRetries) {
    second_handshake_retries_ = 0;
    current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  perform_sock5_second_handshake();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    on_first_handshake_initiated(beast::error_code const ec,
                                 std::size_t const) {
  if (ec) { // could be timeout
    return retry_first_handshake();
  }
  return read_socks5_server_response(true);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    read_socks5_server_response(bool const is_first_handshake) {
  std::memset(reply_buffer, 0, 64);
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds * 3));
  tcp_stream_->async_read_some(
      net::mutable_buffer(reply_buffer, 64),
      [this, is_first_handshake](beast::error_code ec, std::size_t const) {
        on_handshake_response_received(ec, is_first_handshake);
      });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    on_handshake_response_received(beast::error_code ec,
                                   bool const is_first_handshake) {
  if (ec) {
    if (is_first_handshake) {
      return retry_first_handshake();
    }
    return retry_second_handshake();
  }
  char const *p1 = reinterpret_cast<char *>(reply_buffer);
  if (is_first_handshake) {
    if (!p1 || p1[0] != 0x05 || p1[1] != 0x00) {
      current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
      return choose_next_proxy();
    }
    return perform_sock5_second_handshake();
  }
  if (!p1 || p1[1] == 0x01) {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  return send_http_data();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<
    Derived, ProxyProvider>::perform_sock5_second_handshake() {

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
  handshake_buffer.push_back(80 >> 8);
  handshake_buffer.push_back(80 & 0xFF);

  tcp_stream_->expires_after(std::chrono::seconds(10));
  tcp_stream_->async_write_some(
      net::const_buffer(handshake_buffer.data(), handshake_buffer.size()),
      [this](beast::error_code ec, std::size_t const) {
        if (ec) {
          current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
          return choose_next_proxy();
        }
        return read_socks5_server_response(false);
      });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::prepare_request_data(
    bool use_authentication_header) {
  static_cast<Derived *>(this)->prepare_request_data(use_authentication_header);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::choose_next_proxy(
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
    close_stream();
    tcp_stream_.emplace(net::make_strand(io_));
    return connect();
  }
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::
    current_proxy_assign_prop(typename ProxyProvider::Property property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  temp_list_ = {*current_proxy_};
  tcp_stream_->async_connect(
      temp_list_,
      [=](auto const &ec, auto const &ep_type) { on_connected(ec, ep_type); });
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::send_first_request() {
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

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::receive_data() {
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds * 4)); // 4*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(*tcp_stream_, buffer_, response_,
                   beast::bind_front_handler(
                       &socks5_http_socket_base_t::on_data_received, this));
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::start_connect() {
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::send_http_data() {
  tcp_stream_->expires_after(
      std::chrono::milliseconds(utilities::TimeoutMilliseconds));
  http::async_write(*tcp_stream_, request_,
                    beast::bind_front_handler(
                        &socks5_http_socket_base_t::on_data_sent, this));
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::on_data_sent(
    beast::error_code ec, std::size_t const s) {
  if (ec) {
    resend_http_request();
  } else
    receive_data();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProvider::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  send_http_data();
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::close_socket() {
  tcp_stream_->cancel();
  signal_.disconnect_all_slots();
  beast::error_code ec{};
  beast::get_lowest_layer(*tcp_stream_)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(*tcp_stream_).socket().close(ec);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::send_next() {
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
    send_http_data();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived,
                               ProxyProvider>::set_authentication_header() {
  prepare_request_data(true);
}

template <typename Derived, typename ProxyProvider>
void socks5_http_socket_base_t<Derived, ProxyProvider>::on_data_received(
    beast::error_code ec, std::size_t const s) {
  static_cast<Derived *>(this)->data_received(ec, s);
}
} // namespace wudi_server
