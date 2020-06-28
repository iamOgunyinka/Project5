#pragma once

#include "number_stream.hpp"
#include "safe_proxy.hpp"
#include "sockets_interface.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <optional>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;

using tcp = boost::asio::ip::tcp;

template <typename DerivedClass>
class http_socket_base_t : public sockets_interface {
  net::io_context &io_;
  std::optional<beast::tcp_stream> tcp_stream_;
  number_stream_t &numbers_;
  proxy_base_t &proxy_provider_;
  bool &stopped_;

  beast::flat_buffer buffer_{};
  std::size_t connect_count_{};
  int const scans_per_ip_;

protected:
  http::request<http::string_body> request_{};
  http::response<http::string_body> response_{};
  std::string current_number_{};
  proxy_base_t::value_type current_proxy_{nullptr};

protected:
  void close_socket();
  void connect();
  void receive_data();
  void reconnect();
  void close_stream();
  void choose_next_proxy(bool is_first_request = false);
  void send_http_data();
  void set_authentication_header();
  void on_data_sent(beast::error_code, std::size_t const);
  void current_proxy_assign_prop(proxy_base_t::Property);
  void prepare_request_data(bool use_auth = false);
  void on_data_received(beast::error_code, std::size_t const);
  void send_first_request();
  virtual void on_connected(beast::error_code);
  void send_next();

public:
  http_socket_base_t(bool &stopped, net::io_context &, proxy_base_t &,
                     number_stream_t &, int);
  void start_connect() override;

public:
  virtual ~http_socket_base_t();
};

template <typename DerivedClass>
http_socket_base_t<DerivedClass>::http_socket_base_t(
    bool &stopped, net::io_context &io_context, proxy_base_t &proxy_provider,
    number_stream_t &numbers, int const per_ip)
    : sockets_interface{}, io_{io_context},
      tcp_stream_(std::in_place, net::make_strand(io_)), numbers_{numbers},
      proxy_provider_{proxy_provider}, stopped_{stopped},
      scans_per_ip_(per_ip) {}

template <typename DerivedClass>
http_socket_base_t<DerivedClass>::~http_socket_base_t() {
  close_socket();
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::close_socket() {
  tcp_stream_->cancel();
  signal_.disconnect_all_slots();
  beast::error_code ec{};
  beast::get_lowest_layer(*tcp_stream_)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(*tcp_stream_).socket().close(ec);
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::send_http_data() {
  tcp_stream_->expires_after(
      std::chrono::milliseconds(socket_constants_e::timeout_millisecs));
  http::async_write(
      *tcp_stream_, request_,
      beast::bind_front_handler(&http_socket_base_t::on_data_sent, this));
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::on_data_sent(beast::error_code ec,
                                                    std::size_t const s) {
  if (ec) {
    current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  receive_data();
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::receive_data() {
  tcp_stream_->expires_after(std::chrono::milliseconds(
      socket_constants_e::timeout_millisecs * 4)); // 4*3secs
  response_ = {};
  buffer_ = {};
  http::async_read(
      *tcp_stream_, buffer_, response_,
      beast::bind_front_handler(&http_socket_base_t::on_data_received, this));
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::start_connect() {
  choose_next_proxy(true);
  if (current_proxy_)
    send_first_request();
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::send_first_request() {
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
  } catch (empty_container_exception_t &) {
  }
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::send_next() {
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

    if (scans_per_ip_ != 0 && current_proxy_->number_scanned >= scans_per_ip_) {
      current_proxy_assign_prop(proxy_base_t::Property::ProxyMaxedOut);
      return choose_next_proxy();
    }
    ++current_proxy_->number_scanned;
    return send_http_data();
  } catch (empty_container_exception_t &) {
  }
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::reconnect() {
  ++connect_count_;
  if (connect_count_ >= socket_constants_e::max_retries) {
    current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
    return choose_next_proxy();
  }
  connect();
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::connect() {
  if (!current_proxy_ || stopped_) {
    if (stopped_ && !current_number_.empty())
      numbers_.push_back(current_number_);
    current_number_.clear();
    return;
  }
  tcp_stream_->expires_after(
      std::chrono::milliseconds(socket_constants_e::timeout_millisecs));
  tcp_stream_->async_connect(
      *current_proxy_,
      beast::bind_front_handler(&http_socket_base_t::on_connected, this));
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::on_connected(beast::error_code ec) {
  if (ec)
    return reconnect();
  send_http_data();
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::close_stream() {
  tcp_stream_->close();
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::choose_next_proxy(
    bool const is_first_request) {
  connect_count_ = 0;
  current_proxy_ = proxy_provider_.next_endpoint();
  if (!current_proxy_) {
    numbers_.push_back(current_number_);
    current_number_.clear();
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (!is_first_request) {
    tcp_stream_->close();
    tcp_stream_.emplace(net::make_strand(io_));
    return connect();
  }
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::current_proxy_assign_prop(
    proxy_base_t::Property property) {
  if (current_proxy_)
    current_proxy_->property = property;
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::prepare_request_data(
    bool const use_auth) {
  static_cast<DerivedClass *>(this)->prepare_request_data(use_auth);
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::on_data_received(
    beast::error_code ec, std::size_t const bytes_received) {
  static_cast<DerivedClass *>(this)->data_received(ec, bytes_received);
}

template <typename DerivedClass>
void http_socket_base_t<DerivedClass>::set_authentication_header() {
  prepare_request_data(true);
}
} // namespace wudi_server
