#pragma once

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

using utilities::search_result_type_e;
using tcp = boost::asio::ip::tcp;
using beast::error_code;
using utilities::search_result_type_e;

template <typename Derived, typename Proxy> class https_socket_base_t {
protected:
  net::io_context &io_;
  utilities::number_stream_t &numbers_;
  Proxy &proxy_provider_;
  ssl::context &ssl_context_;
  beast::ssl_stream<beast::tcp_stream> ssl_stream_;
  bool &stopped_;

  std::optional<beast::flat_buffer> general_buffer_{};
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  boost::signals2::signal<void(search_result_type_e, std::string_view)> signal_;
  std::size_t connect_count_{};
  std::vector<tcp::endpoint> temp_list_;
  std::size_t send_count_{};
  endpoint_ptr current_proxy_{nullptr};

protected:
  void send_first_request();
  void perform_ssl_handshake();
  void set_authentication_header();
  void on_ssl_handshake(error_code);

  void close_socket();
  void connect();
  void receive_data();
  void reconnect();
  void resend_http_request();
  void choose_next_proxy(bool first_request = false);
  void send_https_data();
  void on_data_sent(error_code, std::size_t const);
  void current_proxy_assign_prop(typename Proxy::Property);
  void prepare_request_data(bool use_auth = false);
  void on_connected(error_code, tcp::resolver::results_type::endpoint_type);
  virtual void send_next();
  void on_data_received(error_code, std::size_t const);
  std::string hostname() const;

public:
  https_socket_base_t(net::ssl::context &, bool &stopped, net::io_context &,
                      Proxy &, utilities::number_stream_t &);
  void start_connect();
  ~https_socket_base_t() {
    signal_.disconnect_all_slots();
    close_socket();
  }
  auto &signal() { return signal_; }
};

template <typename Derived, typename Proxy>
https_socket_base_t<Derived, Proxy>::https_socket_base_t(
    net::ssl::context &ssl_context, bool &stopped, net::io_context &io_context,
    Proxy &proxy_provider, utilities::number_stream_t &numbers)
    : io_{io_context}, ssl_stream_{net::make_strand(io_), ssl_context},
      numbers_{numbers}, proxy_provider_{proxy_provider},
      ssl_context_{ssl_context}, stopped_{stopped} {}

template <typename Derived, typename Proxy>
std::string https_socket_base_t<Derived, Proxy>::hostname() const {
  return static_cast<Derived const *>(this)->hostname();
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::on_connected(
    error_code ec, tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    return reconnect();
  }
  perform_ssl_handshake();
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::perform_ssl_handshake() {
  beast::get_lowest_layer(ssl_stream_).expires_after(std::chrono::seconds(20));
  ssl_stream_.async_handshake(
      net::ssl::stream_base::client,
      [=](beast::error_code ec) { return on_ssl_handshake(ec); });
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::on_ssl_handshake(error_code ec) {
  if (ec.category() == net::error::get_ssl_category() &&
      ec.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ)) {
    return send_https_data();
  }
  if (ec) {
    spdlog::error("SSL handshake: {}", ec.message());
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  send_https_data();
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::prepare_request_data(
    bool use_authentication_header) {
  static_cast<Derived *>(this)->prepare_request_data(use_authentication_header);
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::choose_next_proxy(
    bool const is_first_request) {
  send_count_ = 0;
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (!is_first_request) {
    ssl_stream_.async_shutdown([=](beast::error_code) { return connect(); });
  }
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::current_proxy_assign_prop(
    typename Proxy::Property property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::connect() {
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

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::send_first_request() {
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

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::receive_data() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(std::chrono::milliseconds(utilities::TimeoutMilliseconds *
                                               4)); // 4*3secs
  response_ = {};
  general_buffer_.emplace();
  http::async_read(
      ssl_stream_, *general_buffer_, response_,
      beast::bind_front_handler(&https_socket_base_t::on_data_received, this));
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::start_connect() {
  if (!SSL_set_tlsext_host_name(ssl_stream_.native_handle(),
                                hostname().c_str())) {
    beast::error_code ec{static_cast<int>(::ERR_get_error()),
                         net::error::get_ssl_category()};
    return spdlog::error("Unable to set TLS because: {}", ec.message());
  }
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::send_https_data() {
  beast::get_lowest_layer(ssl_stream_)
      .expires_after(
          std::chrono::milliseconds(utilities::TimeoutMilliseconds * 3));
  http::async_write(
      ssl_stream_, request_,
      beast::bind_front_handler(&https_socket_base_t::on_data_sent, this));
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::on_data_sent(beast::error_code ec,
                                                       std::size_t const s) {
  spdlog::info("Sending HTTPS data");
  if (ec) {
    spdlog::error(ec.message());
    resend_http_request();
  } else {
    receive_data();
  }
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::resend_http_request() {
  if (++send_count_ >= utilities::MaxRetries) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    return choose_next_proxy();
  } else {
    send_https_data();
  }
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::close_socket() {
  beast::get_lowest_layer(ssl_stream_).cancel();
  beast::error_code ec{};
  ec = {};
  ssl_stream_.async_shutdown([this](beast::error_code) {
    beast::get_lowest_layer(ssl_stream_).close();
  });
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::send_next() {
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

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::set_authentication_header() {
  prepare_request_data(true);
}

template <typename Derived, typename Proxy>
void https_socket_base_t<Derived, Proxy>::on_data_received(
    beast::error_code ec, std::size_t const s) {
  spdlog::info("Here");
  static_cast<Derived *>(this)->data_received(ec, s);
}

} // namespace wudi_server
